#include <stdexcept>

#include <mimalloc.h>

#include <catimpl.h>

Nucleus::Nucleus() : enzyme_finders(nullptr), enzyme_adapters(nullptr) {
    cat_ptr<ITable> finders;
    create_table(0, finders.put());
    enzyme_finders = TableView<ITable>(std::move(finders));

    cat_ptr<ITable> adapters;
    create_table(1, adapters.put());
    enzyme_adapters = TableView<ITable>(std::move(adapters));

//    cat_ptr<IEnzymeAdapter> csv1;
//    create_catsyn_v1_enzyme_adapter(csv1.put());
//    enzyme_adapters.set("catsyn::v1", csv1.get());
}

void Nucleus::clone(IObject** out) const noexcept {
    not_implemented();
}

IFactory* Nucleus::get_factory() noexcept {
    return this;
}

ILogger* Nucleus::get_logger() noexcept {
    return &logger;
}

ITable* Nucleus::get_enzyme_finders() noexcept {
    return enzyme_finders.table.get();
}

ITable* Nucleus::get_enzyme_adapters() noexcept {
    return enzyme_adapters.table.get();
}

void Nucleus::calling_thread_init() noexcept {
    thread_init();
}

CAT_API void catsyn::create_nucleus(INucleus** out) {
    thread_init();
    mi_option_set_enabled_default(mi_option_large_os_pages, true);

    *out = new Nucleus;
    (*out)->add_ref();
}
