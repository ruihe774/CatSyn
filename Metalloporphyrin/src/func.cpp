#include <string>

#include <boost/container/flat_set.hpp>

#include <porphyrin.h>

VSFunc::VSFunc(VSPublicFunction func, void* userData, VSFreeFuncData freer,
               std::optional<std::vector<catsyn::ArgSpec>> specs) noexcept
    : func(func), userData(userData), freer(freer), specs(std::move(specs)) {}

VSFunc::~VSFunc() {
    if (freer)
        freer(userData);
}

const catsyn::ArgSpec* VSFunc::get_arg_specs(size_t* len) const noexcept {
    if (specs) {
        *len = specs->size();
        return specs->data();
    } else {
        *len = 0;
        return nullptr;
    }
}

const std::type_info* VSFunc::get_out_type() const noexcept {
    return nullptr;
}

void VSFunc::invoke(catsyn::ITable* args, const IObject** out) {
    auto arg_map = std::make_unique<VSMap>(args);
    catsyn::cat_ptr<catsyn::ITable> result_table;
    core->nucl->get_factory()->create_table(0, result_table.put());
    auto result_map = std::make_unique<VSMap>(std::move(result_table));

    func(arg_map.get(), result_map.get(), userData, core.get(), &api);

    if (auto err = getError(result_map.get()); err)
        throw std::runtime_error(std::string{err});

    auto result = std::move(result_map->table);
    if (auto filter = dynamic_cast<const catsyn::IFilter*>(result->get(result->find("clip"), nullptr)); filter) {
        *out = filter;
        filter->add_ref();
    } else
        *out = result.detach();
}

VSFuncRef* createFunc(VSPublicFunction func, void* userData, VSFreeFuncData freer, VSCore*, const VSAPI*) noexcept {
    return new VSFuncRef{new VSFunc{func, userData, freer}};
}

VSFuncRef* cloneFuncRef(VSFuncRef* f) noexcept {
    return new VSFuncRef{f->func};
}

void callFunc(VSFuncRef* func, const VSMap* in, VSMap* out, VSCore*, const VSAPI* vsapi) noexcept {
    auto& f = func->func;
    catsyn::cat_ptr<const catsyn::IObject> result;

    try {
        f->invoke(in->table.clone().get(), result.put_const());
    } catch (std::exception& exc) {
        setError(out, exc.what());
        return;
    }

    if (auto filter = result.try_query<const catsyn::IFilter>(); filter) {
        out->get_mut()->set(out->table->find("clip"), core->nucl->register_filter(filter.get()), "clip");
    } else {
        auto table = result.query<const catsyn::ITable>();
        for (size_t ref = table->next(catsyn::ITable::npos); ref != catsyn::ITable::npos; ref = table->next(ref)) {
            const char* key = nullptr;
            auto val = table->get(ref, &key);
            out->get_mut()->set(catsyn::ITable::npos, val, key);
        }
    }
}

void freeFunc(VSFuncRef* f) noexcept {
    delete f;
}

bool force_single_threaded_for_this_filter = false;
static const class ParallelBlacklist {
    boost::container::small_flat_set<std::string_view, 8> list;

  public:
    ParallelBlacklist() noexcept {
        list.emplace("club.amusement.eedi2cuda");
        list.emplace("com.wolframrhodium.bm3dcuda");
        list.emplace("com.wolframrhodium.bm3dcuda_rtc");
    }

    bool is_blacklisted(const char* identifier) const noexcept {
        return list.contains(identifier);
    }
} parallel_blacklist;

VSMap* invoke(VSPlugin* plugin, const char* name, const VSMap* args) noexcept {
    auto map = createMap();
    auto funcs = plugin->enzyme->get_functions();
    auto func =
        const_cast<catsyn::IFunction*>(dynamic_cast<const catsyn::IFunction*>(funcs->get(funcs->find(name), nullptr)));
    if (!func) {
        setError(map, "no such function");
        return map;
    }
    VSFuncRef func_ref{func};
    force_single_threaded_for_this_filter = parallel_blacklist.is_blacklisted(plugin->enzyme->get_identifier());
    callFunc(&func_ref, args, map, core.get(), &api);
    return map;
}
