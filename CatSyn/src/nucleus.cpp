#include <stdexcept>

#include <mimalloc.h>

#include <catimpl.h>

Nucleus::Nucleus() : finders(nullptr), ribosomes(nullptr) {
    cat_ptr<ITable> f;
    create_table(0, f.put());
    finders = TableView<ITable>(std::move(f));

    cat_ptr<ITable> r;
    create_table(1, r.put());
    ribosomes = TableView<ITable>(std::move(r));

    cat_ptr<IRibosome> csv1;
    create_catsyn_v1_ribosome(csv1.put());
    ribosomes.set("catsyn::v1", csv1.get());
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
    return finders.table.get();
}

ITable* Nucleus::get_ribosomes() noexcept {
    return ribosomes.table.get();
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
