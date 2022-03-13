#include <stdexcept>

#include <mimalloc.h>

#include <catimpl.h>

Nucleus::Nucleus() : finders(nullptr), ribosomes(nullptr), enzymes(nullptr) {
    cat_ptr<ITable> t;
    create_table(0, t.put());
    finders = decltype(finders)(t.query<Table>());

    create_table(1, t.put());
    ribosomes = decltype(ribosomes)(t.query<Table>());

    cat_ptr<IRibosome> csv1;
    create_catsyn_v1_ribosome(csv1.put());
    ribosomes.set(csv1->get_identifier(), csv1.get());

    create_table(0, t.put());
    enzymes = decltype(enzymes)(t.query<Table>());
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

ITable* Nucleus::get_enzymes() noexcept {
    return enzymes.table.get();
}

void Nucleus::calling_thread_init() noexcept {
    thread_init();
}

CAT_API void catsyn::create_nucleus(INucleus** out) {
    thread_init();
    mi_option_set_enabled_default(mi_option_large_os_pages, true);

    create_instance<Nucleus>(out);
}

#ifdef _WIN32
CAT_API void* runtime_dynamic_cast(const std::type_info& dst_type, IObject* src) {
    // XXX: I don't know this is correct or not
    return __RTDynamicCast(src, 0, (void*)&typeid(IObject), (void*)&dst_type, 0);
}
#endif
