#include <porphyrin.h>

VSFunc::VSFunc(VSCore* core, VSPublicFunction func, void* userData, VSFreeFuncData freer,
               std::optional<std::vector<catsyn::ArgSpec>> specs) noexcept
    : core(core), func(func), userData(userData), freer(freer), specs(std::move(specs)) {}

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
    if (specs)
        catsyn::check_args(this, args);

    auto arg_map = std::make_unique<VSMap>(args);
    catsyn::cat_ptr<catsyn::ITable> result_table;
    core->nucl->get_factory()->create_table(0, result_table.put());
    auto result_map = std::make_unique<VSMap>(std::move(result_table));

    func(arg_map.get(), result_map.get(), userData, core, &api);

    if (auto err = getError(result_map.get()); err)
        throw std::runtime_error(err);

    auto result = std::move(result_map->get_mut());
    try {
        *out = result.get<catsyn::IFilter>("clip").detach();
    } catch (std::bad_cast&) {
    }
    if (!*out)
        *out = result.table.detach();
}

VSFuncRef* createFunc(VSPublicFunction func, void* userData, VSFreeFuncData freer, VSCore* core,
                      const VSAPI*) noexcept {
    return new VSFuncRef{new VSFunc{core, func, userData, freer}};
}

VSFuncRef* cloneFuncRef(VSFuncRef* f) noexcept {
    return new VSFuncRef{f->func};
}

void callFunc(VSFuncRef* func, const VSMap* in, VSMap* out, VSCore* core, const VSAPI* vsapi) noexcept {
    auto& f = func->func;
    auto arg_table = catsyn::TableView<catsyn::ITable>(catsyn::create_arg_table(core->nucl->get_factory(), f.get()));
    auto size = in->view.size();
    for (size_t ref = 0; ref < size; ++ref) {
        auto key = in->view.table->get_key(ref);
        auto val = in->view.table->get(ref);
        if (key && val)
            arg_table.set(key, val);
    }
    catsyn::cat_ptr<const catsyn::IObject> result;

    try {
        f->invoke(arg_table.table.get(), result.put_const());
    } catch (...) {
        setError(out, catsyn::exception_ptr_what(std::current_exception()));
        return;
    }

    if (auto filter = result.try_query<const catsyn::IFilter>(); filter) {
        catsyn::cat_ptr<catsyn::ISubstrate> substrate;
        core->nucl->register_filter(filter.get(), substrate.put());
        out->get_mut().set("clip", substrate.get());
    } else {
        auto table = result.query<const catsyn::ITable>();
        auto size = table->size();
        for (size_t ref = 0; ref < size; ++ref) {
            out->get_mut().table->set(ref, table->get(ref));
            out->get_mut().table->set_key(ref, table->get_key(ref));
        }
    }
}

void freeFunc(VSFuncRef* f) noexcept {
    delete f;
}

VSMap *invoke(VSPlugin *plugin, const char *name, const VSMap *args) noexcept {
    auto map = createMap();
    int error;
    auto func = propGetFunc(getFunctions(plugin), name, 0, &error);
    if (!func) {
        setError(map, "no such function");
        return map;
    }
    callFunc(func, args, map, plugin->core, &api);
    return map;
}
