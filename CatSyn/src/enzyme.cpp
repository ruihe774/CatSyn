#include <filesystem>
#include <string>

#include <Windows.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/dll.hpp>

#include <catimpl.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

[[noreturn]] static void insufficient_buffer() {
    throw std::runtime_error("insufficient buffer");
}

static std::filesystem::path get_current_dll_filename() noexcept {
    auto current_module = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t wbuf[2048];
    GetModuleFileNameW(current_module, wbuf, sizeof(wbuf));
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        insufficient_buffer();
    return wbuf;
}

class DllEnzymeFinder final : public Object, public IEnzymeFinder {
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
    explicit DllEnzymeFinder(std::filesystem::path path) : path(std::move(path)) {}
    explicit DllEnzymeFinder(const char* path) : path(normalize(path)) {}
    void clone(IObject** out) const noexcept final {
        *out = new DllEnzymeFinder(path);
        (*out)->add_ref();
    }

    size_t find_enzyme(const char* const** out) noexcept final {
        if (!tokens_c_str) {
            const std::string prefix = "dll:";
            if (path.has_filename())
                tokens.emplace_back(prefix + path.string());
            else
                try {
                    for (auto&& entry : std::filesystem::directory_iterator(path))
                        if (auto&& dll_path = entry.path();
                            entry.is_regular_file() && boost::iequals(dll_path.extension().string(), ".dll"))
                            tokens.emplace_back(prefix + dll_path.string());
                } catch (std::filesystem::filesystem_error& err) {
                }
            tokens_c_str = std::unique_ptr<const char*[]>(new const char*[tokens.size()]);
            for (size_t i = 0; i < tokens.size(); ++i)
                tokens_c_str[i] = tokens[i].c_str();
        }
        *out = tokens_c_str.get();
        return tokens.size();
    }
};

void Nucleus::create_dll_enzyme_finder(const char* path, IEnzymeFinder** out) noexcept {
    *out = new DllEnzymeFinder(path);
    (*out)->add_ref();
}

class CatSynV1EnzymeAdaptor final : public Object, public IEnzymeAdapter, public Shuttle {
    std::vector<boost::dll::shared_library> loaded;

  public:
    void load_enzyme(const char* token, IObject** out) noexcept final {
        *out = nullptr;
        if (boost::starts_with(token, "dll:"))
            try {
                boost::dll::shared_library lib(token + 4);
                auto init_func = lib.get<void(INucleus*, IObject**)>(
                    "?catsyn_enzyme_init@@YAXPEAVINucleus@catsyn@@PEAPEAVIObject@2@@Z");
                init_func(&this->nucl, out);
                if (*out)
                    loaded.emplace_back(std::move(lib));
            } catch (boost::dll::fs::system_error&) {
            }
    }

    void clone(IObject** out) const noexcept final {
        *out = new CatSynV1EnzymeAdaptor(this->nucl);
        (*out)->add_ref();
    }

    explicit CatSynV1EnzymeAdaptor(Nucleus& nucl) : Shuttle(nucl) {}
};

void Nucleus::create_catsyn_v1_enzyme_adapter(IEnzymeAdapter** out) noexcept {
    *out = new CatSynV1EnzymeAdaptor(*this);
    (*out)->add_ref();
}
