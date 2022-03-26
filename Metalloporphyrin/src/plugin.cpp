#include <algorithm>
#include <string>

#include <string.h>

#include <Windows.h>

#include <porphyrin.h>

constexpr auto npos = catsyn::ITable::npos;

struct VSEnzyme final : public Object, public catsyn::IEnzyme {
    catsyn::cat_ptr<catsyn::ITable> funcs;
    std::string path;
    std::string identifier;
    std::string ns;
    const char* get_identifier() const noexcept final {
        return identifier.c_str();
    }
    const char* get_namespace() const noexcept final {
        return ns.c_str();
    }
    const catsyn::ITable* get_functions() const noexcept final {
        return funcs.get();
    }

    explicit VSEnzyme(const char* path) noexcept : path(path), funcs(nullptr) {
        core->nucl->get_factory()->create_table(0, funcs.put());
#ifdef _WIN32
        std::replace(this->path.begin(), this->path.end(), '\\', '/');
#endif
    }
};

void registerFunction(const char* name, const char* args, VSPublicFunction argsFunc, void* functionData,
                      VSPlugin* plugin) noexcept {
    auto vse = plugin->enzyme.query<VSEnzyme>();
    vse->funcs->set(vse->funcs->find(name), new VSFunc{argsFunc, functionData, nullptr, std::nullopt}, name);
    plugin->arg_strs[name] = args;
}

static void configurePlugin(const char* identifier, const char* defaultNamespace, const char*, int, int,
                            VSPlugin* plugin) noexcept {
    auto vse = plugin->enzyme.query<VSEnzyme>();
    vse->identifier = identifier;
    vse->ns = defaultNamespace;
}

VSPlugin* getPluginById(const char* identifier, VSCore*) noexcept {
    {
        std::shared_lock<std::shared_mutex> lock(core->plugins_mutex);
        if (auto it = core->plugins.find(identifier); it != core->plugins.end())
            return it->second.get();
    }
    auto enzymes = core->nucl->get_enzymes();
    if (auto enzyme = const_cast<catsyn::IEnzyme*>(
            dynamic_cast<const catsyn::IEnzyme*>(enzymes->get(enzymes->find(identifier), nullptr)));
        enzyme) {
        std::unique_lock<std::shared_mutex> lock(core->plugins_mutex);
        auto plugin = core->plugins.emplace(identifier, new VSPlugin{enzyme}).first->second.get();
        core->ns_map.emplace(enzyme->get_namespace(), plugin);
        return plugin;
    } else
        return nullptr;
}

VSPlugin* getPluginByNs(const char* ns, VSCore*) noexcept {
    {
        std::shared_lock<std::shared_mutex> lock(core->plugins_mutex);
        if (auto it = core->ns_map.find(ns); it != core->ns_map.end())
            return it->second;
    }
    auto enzymes = core->nucl->get_enzymes();
    for (auto ref = enzymes->next(npos); ref != npos; ref = enzymes->next(ref)) {
        auto enzyme = const_cast<catsyn::IEnzyme*>(dynamic_cast<const catsyn::IEnzyme*>(enzymes->get(ref, nullptr)));
        if (strcmp(enzyme->get_namespace(), ns) == 0) {
            std::unique_lock<std::shared_mutex> lock(core->plugins_mutex);
            auto plugin = core->plugins.emplace(enzyme->get_identifier(), new VSPlugin{enzyme}).first->second.get();
            core->ns_map.emplace(ns, plugin);
            return plugin;
        }
    }
    return nullptr;
}

VSMap* getPlugins(VSCore*) noexcept {
    catsyn::cat_ptr<catsyn::ITable> table;
    auto plugins = core->nucl->get_enzymes();
    auto size = plugins->size();
    auto factory = core->nucl->get_factory();
    factory->create_table(size, table.put());
    for (size_t ref = plugins->next(npos); ref != npos; ref = plugins->next(ref))
        if (auto enzyme = &dynamic_cast<const catsyn::IEnzyme&>(*plugins->get(ref, nullptr)); enzyme) {
            auto identifier = enzyme->get_identifier();
            auto ns = enzyme->get_namespace();
            auto id_len = strlen(identifier);
            auto ns_len = strlen(ns);
            catsyn::cat_ptr<catsyn::IBytes> bytes;
            factory->create_bytes(nullptr, id_len + ns_len + 3, bytes.put());
            auto pb = static_cast<char*>(bytes->data());
            memcpy(pb, ns, ns_len);
            pb[ns_len] = ';';
            memcpy(pb + ns_len + 1, identifier, id_len);
            pb[ns_len + id_len + 1] = ';';
            pb[ns_len + id_len + 2] = '\0';
            table->set(npos, bytes.get(), identifier);
        }
    return new VSMap{std::move(table)};
}

VSMap* getFunctions(VSPlugin* plugin) noexcept {
    // TODO: support non-VS filters
    catsyn::cat_ptr<catsyn::ITable> table;
    auto factory = core->nucl->get_factory();
    auto functions = plugin->enzyme->get_functions();
    auto size = functions->size();
    factory->create_table(size, table.put());
    const char* key;
    for (size_t ref = functions->next(npos); ref != npos; ref = functions->next(ref))
        if (functions->get(ref, &key) && key) {
            auto key_len = strlen(key);
            catsyn::cat_ptr<catsyn::IBytes> bytes;
            const auto& arg_str = plugin->arg_strs[key];
            factory->create_bytes(nullptr, key_len + arg_str.size() + 2, bytes.put());
            auto pb = static_cast<char*>(bytes->data());
            memcpy(pb, key, key_len);
            pb[key_len] = ';';
            memcpy(pb + key_len + 1, arg_str.c_str(), arg_str.size() + 1);
            table->set(npos, bytes.get(), key);
        }
    return new VSMap{std::move(table)};
}

const char* getPluginPath(const VSPlugin* plugin) noexcept {
    try {
        return plugin->enzyme.query<VSEnzyme>()->path.c_str();
    } catch (std::bad_cast&) {
        core->nucl->get_logger()->log(catsyn::LogLevel::WARNING,
                                      "Metalloporphyrin: cannot retrieve path for non-VS enzyme (getPluginPath)");
        return nullptr;
    }
}

const char* VSRibosome::get_identifier() const noexcept {
    return "club.yusyabu.metalloporphyrin.api3";
}

[[noreturn]] static void insufficient_buffer() {
    throw std::runtime_error("insufficient buffer");
}

static wchar_t* u2w(const char* s) noexcept {
    thread_local wchar_t wbuf[2048];
    auto wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, wbuf, sizeof(wbuf));
    if (wlen == 0)
        insufficient_buffer();
    return wbuf;
}

void VSRibosome::synthesize_enzyme(const char* token, catsyn::IObject** out) noexcept {
    *out = nullptr;
    if (std::string_view{token}.starts_with("dll:")) {
        auto lib = LoadLibraryExW(u2w(token + 4), nullptr,
                                  LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
        if (lib == nullptr) {
            FreeLibrary(lib);
            return;
        }
        auto init_func = reinterpret_cast<VSInitPlugin>(GetProcAddress(lib, "VapourSynthPluginInit"));
        if (!init_func) {
            FreeLibrary(lib);
            return;
        }
        std::unique_ptr<VSPlugin> vsp(new VSPlugin{new VSEnzyme{token + 4}});
        init_func(configurePlugin, registerFunction, vsp.get());
        // loaded.emplace(vsp->enzyme.get(), std::move(lib));
        auto ns = vsp->enzyme->get_namespace();
        auto id = vsp->enzyme->get_identifier();
        *out = vsp->enzyme.get();
        (*out)->add_ref();
        std::lock_guard<std::shared_mutex> guard(core->plugins_mutex);
        core->ns_map.emplace(ns, core->plugins.emplace(id, std::move(vsp)).first->second.get());
    }
}

[[noreturn]] static void hydrolyze_non_unique() {
    throw std::runtime_error("attempt to hydrolyze an enzyme by non-unique reference");
}

void VSRibosome::hydrolyze_enzyme(catsyn::IObject** inout) noexcept {
//    auto it = loaded.find(*inout);
//    if (it != loaded.end()) {
//        if (!(*inout)->is_unique())
//            hydrolyze_non_unique();
//        (*inout)->release();
//        *inout = nullptr;
//        loaded.erase(it);
//    }
}
