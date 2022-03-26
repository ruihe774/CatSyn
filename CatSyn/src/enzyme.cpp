#include <filesystem>
#include <map>
#include <string_view>

#include <string.h>

#include <Windows.h>

#include <wil/resource.h>

#include <catimpl.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static std::filesystem::path get_current_dll_filename() noexcept {
    auto current_module = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t wbuf[2048];
    GetModuleFileNameW(current_module, wbuf, sizeof(wbuf));
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        insufficient_buffer();
    return wbuf;
}

static wchar_t* u2w(const char* s) noexcept {
    thread_local wchar_t wbuf[2048];
    auto wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, wbuf, sizeof(wbuf));
    if (wlen == 0)
        insufficient_buffer();
    return wbuf;
}

class DllEnzymeFinder final : public Object, virtual public IEnzymeFinder, public Shuttle {
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
                            entry.is_regular_file() && _wcsicmp(dll_path.extension().c_str(), L".dll") == 0)
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

class CatSynV1Ribosome final : public Object, virtual public IRibosome, public Shuttle {
    std::map<IObject*, wil::unique_hmodule> loaded;

    [[noreturn]] static void hydrolyze_non_unique() {
        throw std::runtime_error("attempt to hydrolyze an enzyme by non-unique reference");
    }

  public:
    const char* get_identifier() const noexcept final {
        return "club.yusyabu.catsyn.v1";
    }

    void synthesize_enzyme(const char* token, IObject** out) noexcept final {
        *out = nullptr;
        if (std::string_view{token}.starts_with("dll:")) {
            auto lib = wil::unique_hmodule{LoadLibraryExW(
                u2w(token + 4), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)};
            if (!lib)
                return;
            auto init_func = reinterpret_cast<void (*)(INucleus*, IObject**)>(
                GetProcAddress(lib.get(), "?catsyn_enzyme_init@@YAXPEAVINucleus@catsyn@@PEAPEAVIObject@2@@Z"));
            if (!init_func)
                return;
            init_func(&this->nucl, out);
            if (*out)
                loaded.emplace(*out, std::move(lib));
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
    for (size_t ref = finders->next(ITable::npos); ref != ITable::npos; ref = finders->next(ref)) {
        auto finder = &const_cast<IEnzymeFinder&>(dynamic_cast<const IEnzymeFinder&>(*finders->get(ref, nullptr)));
        size_t size;
        auto p = finder->find(&size);
        tokens.reserve(tokens.size() + size);
        for (size_t i = 0; i < size; ++i)
            tokens.emplace_back(p[i]);
    }
    dedup(tokens);
    std::map<std::string_view, cat_ptr<IEnzyme>> ezs;
    for (auto token_sv : tokens) {
        // we are sure that tokens are null terminated!
        auto token = token_sv.data();
        for (size_t ref = ribosomes->next(ITable::npos); ref != ITable::npos; ref = ribosomes->next(ref)) {
            auto ribosome = &const_cast<IRibosome&>(dynamic_cast<const IRibosome&>(*ribosomes->get(ref, nullptr)));
            cat_ptr<IObject> obj;
            ribosome->synthesize_enzyme(token, obj.put());
            if (obj) {
                // TODO: hydrolyze overwritten enzymes and ribosomes
                if (auto enzyme = obj.try_query<IEnzyme>(); enzyme) {
                    std::string_view id = enzyme->get_identifier();
                    auto jt = ezs.find(id);
                    if (jt != ezs.end())
                        logger.log(LogLevel::WARNING,
                                   format_c("Nucleus: enzyme '{}' is registered multiple times", id));
                    ezs.emplace_hint(jt, id, std::move(enzyme));
                } else if (auto ribosome = obj.try_query<IRibosome>(); ribosome) {
                    auto id = ribosome->get_identifier();
                    auto ref = ribosomes->find(id);
                    if (ref != ITable::npos)
                        logger.log(LogLevel::WARNING,
                                   format_c("Nucleus: ribosome '{}' is registered multiple times", id));
                    ribosomes->set(ref, ribosome.get(), id);
                } else
                    not_enzyme_nor_ribosome();
                goto synthesized;
            }
        }
        logger.log(LogLevel::WARNING, format_c("Nucleus: enzyme with token '{}' cannot be synthesized", token));
    synthesized:;
    }
    for (const auto& entry : ezs)
        // we are sure that keys are null terminated!
        enzymes->set(ITable::npos, entry.second.get(), entry.first.data());

    auto new_refcount = this->acquire_refcount();
    if (old_refcount != new_refcount)
        logger.log(LogLevel::WARNING, "Nucleus: reference count changed during enzyme synthesis! "
                                      "Some enzymes may added reference to nucleus, which is not allowed");
}
