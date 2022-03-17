#include <string.h>

#include <porphyrin.h>

struct VSEnzyme : public Object, public catsyn::IEnzyme {
    catsyn::TableView<catsyn::ITable> funcs;
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
    return new VSMap{core->nucl->get_enzymes()};
}

VSMap* getFunctions(VSPlugin* plugin) noexcept {
    return new VSMap{plugin->enzyme->get_functions()};
}

const char *getPluginPath(const VSPlugin *plugin) noexcept {
    // TODO
    return nullptr;
}
