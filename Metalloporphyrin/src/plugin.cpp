#include <string>

#include <string.h>

#include <boost/algorithm/string/predicate.hpp>

#include <Windows.h>

#include <porphyrin.h>

struct VSEnzyme final : public Object, public catsyn::IEnzyme {
    catsyn::TableView<catsyn::ITable> funcs;
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
        return funcs.table.get();
    }

    VSEnzyme(catsyn::INucleus& nucl, const char* path) noexcept : path(path), funcs(nullptr) {
        nucl.get_factory()->create_table(0, funcs.table.put());
    }
};

static std::vector<catsyn::ArgSpec> args_vs_to_cs(VSPlugin* plugin, const char* args) {
    std::vector<catsyn::ArgSpec> specs;
    auto len = strlen(args);
    memcpy_s(plugin->pargs, sizeof(VSPlugin::args_buf) - (plugin->pargs - plugin->args_buf), args, len + 1);
    auto p = plugin->pargs;
    plugin->pargs += len + 1;
    auto s = p;
    int phase = 0;
    const char* name;
    const std::type_info* ti;
    bool required;
    for (;; ++p) {
        auto ch = *p;
        switch (ch) {
        case ':':
        case ';':
        case '[':
        case '\0':
            *p = '\0';
            switch (phase) {
            case 0:
                name = s;
                required = true;
                break;
            case 1:
                if (strcmp(s, "int") == 0 || strcmp(s, "float") == 0)
                    ti = &typeid(catsyn::INumberArray);
                else if (strcmp(s, "data") == 0)
                    ti = &typeid(catsyn::IBytes);
                else if (strcmp(s, "clip") == 0)
                    ti = &typeid(catsyn::ISubstrate);
                else if (strcmp(s, "frame") == 0)
                    ti = &typeid(catsyn::IFrame);
                else if (strcmp(s, "func") == 0)
                    ti = &typeid(catsyn::IFunction);
                else
                    throw std::invalid_argument("unknown argument type");
                break;
            case 2:
                if (strcmp(s, "opt") == 0)
                    required = false;
                break;
            }
            if (ch == '[') {
                p += 2;
                if (*p == ':')
                    phase = 2;
                else {
                    specs.push_back(catsyn::ArgSpec{name, *ti, required});
                    phase = 0;
                }
            } else if (ch == ':')
                ++phase;
            else if (phase) {
                specs.push_back(catsyn::ArgSpec{name, *ti, required});
                phase = 0;
            }
            s = p + 1;
        }
        if (ch == '\0')
            break;
    }
    return specs;
}

void registerFunction(const char* name, const char* args, VSPublicFunction argsFunc, void* functionData,
                      VSPlugin* plugin) noexcept {
    auto vse = plugin->enzyme.query<VSEnzyme>();
    vse->funcs.set(name, new VSFunc{plugin->core, argsFunc, functionData, nullptr, args_vs_to_cs(plugin, args)});
    plugin->arg_strs[name] = args;
}

static void configurePlugin(const char* identifier, const char* defaultNamespace, const char*, int, int,
                            VSPlugin* plugin) noexcept {
    auto vse = plugin->enzyme.query<VSEnzyme>();
    vse->identifier = identifier;
    vse->ns = defaultNamespace;
}

VSPlugin* getPluginById(const char* identifier, VSCore* core) noexcept {
    {
        std::shared_lock<std::shared_mutex> lock(core->plugins_mutex);
        for (auto& plugin : core->plugins)
            if (strcmp(plugin->enzyme->get_identifier(), identifier) == 0)
                return plugin.get();
    }
    auto enzyme = catsyn::get_enzyme_by_id(core->nucl.get(), identifier);
    if (enzyme) {
        std::unique_lock<std::shared_mutex> lock(core->plugins_mutex);
        core->plugins.emplace_back(new VSPlugin{core, enzyme});
        return core->plugins.back().get();
    } else
        return nullptr;
}

VSPlugin* getPluginByNs(const char* ns, VSCore* core) noexcept {
    {
        std::shared_lock<std::shared_mutex> lock(core->plugins_mutex);
        for (auto& plugin : core->plugins)
            if (strcmp(plugin->enzyme->get_namespace(), ns) == 0)
                return plugin.get();
    }
    auto enzyme = catsyn::get_enzyme_by_ns(core->nucl.get(), ns);
    if (enzyme) {
        std::unique_lock<std::shared_mutex> lock(core->plugins_mutex);
        core->plugins.emplace_back(new VSPlugin{core, enzyme});
        return core->plugins.back().get();
    } else
        return nullptr;
}

VSMap* getPlugins(VSCore* core) noexcept {
    catsyn::cat_ptr<catsyn::ITable> table;
    auto plugins = core->nucl->get_enzymes();
    auto size = plugins->size();
    auto factory = core->nucl->get_factory();
    factory->create_table(size, table.put());
    for (size_t old_ref = 0, new_ref = 0; old_ref < size; ++old_ref)
        if (auto enzyme = &dynamic_cast<const catsyn::IEnzyme&>(*plugins->get(old_ref)); enzyme) {
            auto identifier = enzyme->get_identifier();
            auto ns = enzyme->get_namespace();
            auto id_len = strlen(identifier);
            auto ns_len = strlen(ns);
            table->set_key(new_ref, identifier);
            catsyn::cat_ptr<catsyn::IBytes> bytes;
            factory->create_bytes(nullptr, id_len + ns_len + 3, bytes.put());
            auto pb = static_cast<char*>(bytes->data());
            memcpy(pb, ns, ns_len);
            pb[ns_len] = ';';
            memcpy(pb + ns_len + 1, identifier, id_len);
            pb[ns_len + id_len + 1] = ';';
            pb[ns_len + id_len + 2] = '\0';
            table->set(new_ref++, bytes.get());
        }
    return new VSMap{std::move(table)};
}

VSMap* getFunctions(VSPlugin* plugin) noexcept {
    // TODO: support non-VS filters
    catsyn::cat_ptr<catsyn::ITable> table;
    auto factory = plugin->core->nucl->get_factory();
    auto functions = plugin->enzyme->get_functions();
    auto size = functions->size();
    factory->create_table(size, table.put());
    for (size_t old_ref = 0, new_ref = 0; old_ref < size; ++old_ref)
        if (auto key = functions->get_key(old_ref); key) {
            auto key_len = strlen(key);
            table->set_key(new_ref, key);
            catsyn::cat_ptr<catsyn::IBytes> bytes;
            const auto& arg_str = plugin->arg_strs[key];
            factory->create_bytes(nullptr, key_len + arg_str.size() + 2, bytes.put());
            auto pb = static_cast<char*>(bytes->data());
            memcpy(pb, key, key_len);
            pb[key_len] = ';';
            memcpy(pb + key_len + 1, arg_str.data(), arg_str.size());
            pb[key_len + arg_str.size() + 1] = '\0';
            table->set(new_ref++, bytes.get());
        }
    return new VSMap{std::move(table)};
}

const char* getPluginPath(const VSPlugin* plugin) noexcept {
    try {
        return plugin->enzyme.query<VSEnzyme>()->path.c_str();
    } catch (std::bad_cast&) {
        plugin->core->nucl->get_logger()->log(
            catsyn::LogLevel::WARNING, "Metalloporphyrin: cannot retrieve path for non-VS enzyme (getPluginPath)");
        return nullptr;
    }
}

const char* VSRibosome::get_identifier() const noexcept {
    return "club.yusyabu.metalloporphyrin.api3";
}

void VSRibosome::synthesize_enzyme(const char* token, catsyn::IObject** out) noexcept {
    *out = nullptr;
    if (boost::starts_with(token, "dll:"))
        try {
//            boost::dll::shared_library lib(token + 4);
//            auto init_func = lib.get<VSInitPlugin>("VapourSynthPluginInit");
            // TODO: why boost fails?
            auto lib = LoadLibraryA(token + 4);
            auto init_func = reinterpret_cast<VSInitPlugin>(GetProcAddress(lib, "VapourSynthPluginInit"));
            std::unique_ptr<VSPlugin> vsp(new VSPlugin{core, new VSEnzyme{*core->nucl, token + 4}});
            init_func(configurePlugin, registerFunction, vsp.get());
            *out = vsp->enzyme.get();
            (*out)->add_ref();
//            loaded.emplace(*out, std::move(lib));
            std::lock_guard<std::shared_mutex> guard(core->plugins_mutex);
            core->plugins.emplace_back(std::move(vsp));
        } catch (boost::dll::fs::system_error&) {
        }
}

[[noreturn]] static void hydrolyze_non_unique() {
    throw std::runtime_error("attempt to hydrolyze an enzyme by non-unique reference");
}

void VSRibosome::hydrolyze_enzyme(catsyn::IObject** inout) noexcept {
    auto it = loaded.find(*inout);
    if (it != loaded.end()) {
        if (!(*inout)->is_unique())
            hydrolyze_non_unique();
        (*inout)->release();
        *inout = nullptr;
        loaded.erase(it);
    }
}

VSRibosome::VSRibosome(VSCore* core) noexcept : core(core) {}
