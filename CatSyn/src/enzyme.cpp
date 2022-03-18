#include <filesystem>
#include <map>
#include <string_view>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/dll.hpp>

#include <catimpl.h>

#include <Windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static std::filesystem::path get_current_dll_filename() noexcept {
    auto current_module = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t wbuf[2048];
    GetModuleFileNameW(current_module, wbuf, sizeof(wbuf));
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        insufficient_buffer();
    return wbuf;
}

class DllEnzymeFinder final : public Object, public IEnzymeFinder, public Shuttle {
    std::filesystem::path path;
    std::vector<std::string> tokens;
    std::unique_ptr<const char*[]> tokens_c_str;

    static std::filesystem::path normalize(const char* s) noexcept {
        std::filesystem::path path;
        if (s[0] == '@' && (s[1] == '/' || s[1] == '\\')) {
            path = get_current_dll_filename();
            path.remove_filename();
            path += s + 2;
        } else {
            path = s;
        }
        return std::filesystem::absolute(path);
    }

  public:
    DllEnzymeFinder(Nucleus& nucl, std::filesystem::path path) noexcept : Shuttle(nucl), path(std::move(path)) {}
    DllEnzymeFinder(Nucleus& nucl, const char* path) noexcept : Shuttle(nucl), path(normalize(path)) {}

    const char* const* find(size_t* len) noexcept final {
        if (!tokens_c_str) {
            const std::string prefix = "dll:";
            if (path.has_filename()) {
                if (std::filesystem::is_directory(path))
                    nucl.logger.log(LogLevel::WARNING, format_c("DllEnzymeFinder: the given path '{}' is a directory "
                                                                "(hint: append '/' or '\\' to search in directory)",
                                                                path.string()));
                tokens.emplace_back(prefix + path.string());
            } else
                try {
                    for (auto&& entry : std::filesystem::directory_iterator(path))
                        if (auto&& dll_path = entry.path();
                            entry.is_regular_file() && boost::iequals(dll_path.extension().string(), ".dll"))
                            tokens.emplace_back(prefix + dll_path.string());
                } catch (std::filesystem::filesystem_error& err) {
                    this->nucl.logger.log(
                        LogLevel::WARNING,
                        format_c("DllEnzymeFinder: failed to open directory '{}' ({})", path.string(), err));
                }
            tokens_c_str = std::unique_ptr<const char*[]>(new const char*[tokens.size()]);
            for (size_t i = 0; i < tokens.size(); ++i)
                tokens_c_str[i] = tokens[i].c_str();
        }
        *len = tokens.size();
        return tokens_c_str.get();
    }
};

void Nucleus::create_dll_enzyme_finder(const char* path, IEnzymeFinder** out) noexcept {
    create_instance<DllEnzymeFinder>(out, *this, path);
}

class CatSynV1Ribosome final : public Object, public IRibosome, public Shuttle {
    std::map<IObject*, boost::dll::shared_library> loaded;

    [[noreturn]] static void hydrolyze_non_unique() {
        throw std::runtime_error("attempt to hydrolyze an enzyme by non-unique reference");
    }

  public:
    const char* get_identifier() const noexcept final {
        return "club.yusyabu.catsyn.v1";
    }

    void synthesize_enzyme(const char* token, IObject** out) noexcept final {
        *out = nullptr;
        if (boost::starts_with(token, "dll:"))
            try {
                boost::dll::shared_library lib(token + 4);
                auto init_func = lib.get<void(INucleus*, IObject**)>(
                    "?catsyn_enzyme_init@@YAXPEAVINucleus@catsyn@@PEAPEAVIObject@2@@Z");
                init_func(&this->nucl, out);
                if (*out)
                    loaded.emplace(*out, std::move(lib));
            } catch (boost::dll::fs::system_error&) {
            }
    }

    void hydrolyze_enzyme(IObject** inout) noexcept final {
        auto it = loaded.find(*inout);
        if (it != loaded.end()) {
            if (!(*inout)->is_unique())
                hydrolyze_non_unique();
            (*inout)->release();
            *inout = nullptr;
            loaded.erase(it);
        }
    }

    explicit CatSynV1Ribosome(Nucleus& nucl) noexcept : Shuttle(nucl) {}
};

void Nucleus::create_catsyn_v1_ribosome(IRibosome** out) noexcept {
    create_instance<CatSynV1Ribosome>(out, *this);
}

template<typename T> static void dedup(std::vector<T>& vec) noexcept {
    std::vector<size_t> idx;
    idx.reserve(vec.size());
    for (size_t i = 0; i < vec.size(); ++i)
        idx.emplace_back(i);
    std::sort(idx.begin(), idx.end(), [&](size_t l, size_t r) { return vec[l] < vec[r]; });
    idx.erase(std::unique(idx.begin(), idx.end(), [&](size_t l, size_t r) { return vec[l] == vec[r]; }), idx.end());
    std::sort(idx.begin(), idx.end());
    std::vector<T> new_vec;
    new_vec.reserve(idx.size());
    for (auto i : idx)
        new_vec.emplace_back(std::move(vec[i]));
    vec = std::move(new_vec);
}

[[noreturn]] static void not_enzyme_nor_ribosome() {
    throw std::runtime_error("the synthesized product is not enzyme nor ribosome");
}

void Nucleus::synthesize_enzymes() noexcept {
    auto old_refcount = this->acquire_refcount();

    std::vector<std::string_view> tokens;
    for (size_t ref = 0; ref < finders.size(); ++ref) {
        auto finder = finders.get<IEnzymeFinder>(ref);
        if (!finder)
            continue;
        size_t size;
        auto p = finder.clone()->find(&size);
        tokens.reserve(tokens.size() + size);
        for (size_t i = 0; i < size; ++i)
            tokens.emplace_back(p[i]);
    }
    dedup(tokens);
    std::map<std::string_view, cat_ptr<IEnzyme>> ezs;
    for (auto token_sv : tokens) {
        // we are sure that tokens are null terminated!
        auto token = token_sv.data();
        for (size_t ref = 0; ref < ribosomes.size(); ++ref) {
            auto ribosome = ribosomes.get<IRibosome>(ref);
            if (!ribosome)
                continue;
            cat_ptr<IObject> obj;
            ribosome.clone()->synthesize_enzyme(token, obj.put());
            if (obj) {
                // TODO: hydrolyze overwritten enzymes and ribosomes
                if (auto enzyme = obj.try_query<IEnzyme>(); enzyme) {
                    std::string_view id = enzyme->get_identifier();
                    auto jt = ezs.find(id);
                    if (jt != ezs.end())
                        logger.log(LogLevel::WARNING,
                                   format_c("Nucleus: enzyme '{}' is registered multiple times", id));
                    else
                        logger.log(LogLevel::DEBUG, format_c("Nucleus: enzyme '{}' synthesized", id));
                    ezs.emplace_hint(jt, id, std::move(enzyme));
                } else if (auto ribosome = obj.try_query<IRibosome>(); ribosome) {
                    auto id = ribosome->get_identifier();
                    if (ribosomes.get<IObject>(id))
                        logger.log(LogLevel::WARNING,
                                   format_c("Nucleus: ribosome '{}' is registered multiple times", id));
                    else
                        logger.log(LogLevel::DEBUG, format_c("Nucleus: ribosome '{}' synthesized", id));
                    ribosomes.set(id, ribosome.get());
                } else
                    not_enzyme_nor_ribosome();
                goto synthesized;
            }
        }
        logger.log(LogLevel::WARNING, format_c("Nucleus: enzyme with token '{}' cannot be synthesized", token));
    synthesized:;
    }
    size_t ref = 0;
    for (const auto& entry : ezs) {
        // we are sure that keys are null terminated!
        enzymes.table->set_key(ref, entry.first.data());
        enzymes.table->set(ref, entry.second.get());
        ++ref;
    }

    auto new_refcount = this->acquire_refcount();
    if (old_refcount != new_refcount)
        logger.log(LogLevel::WARNING, "Nucleus: reference count changed during enzyme synthesis! "
                                      "Some enzymes may added reference to nucleus, which is not allowed");
}
