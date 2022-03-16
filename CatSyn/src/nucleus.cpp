#include <mimalloc.h>

#include <catimpl.h>

NucleusConfig get_default_config() noexcept {
    return NucleusConfig{std::thread::hardware_concurrency(), 4096};
}

Nucleus::Nucleus() {
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

Nucleus::AllocStat::~AllocStat() {
    if (auto cur = get_current(); cur != 0)
        // this is dirty
        (reinterpret_cast<Logger*>(this) - 1)->log(LogLevel::WARNING, format_c("Nucleus: {}B memory not freed!", cur));
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

[[noreturn]] static void throw_set_config_when_reacting() {
    throw std::logic_error("changing config is not allowed during reaction");
}

void Nucleus::set_config(NucleusConfig cfg) noexcept {
    // XXX: may race
    if (is_reacting())
        throw_set_config_when_reacting();
    config = cfg;
}

NucleusConfig Nucleus::get_config() const noexcept {
    return config;
}

CAT_API Version catsyn::get_version() noexcept {
    return version;
}

CAT_API void catsyn::create_nucleus(INucleus** out) noexcept {
    thread_init();
    mi_option_set_enabled_default(mi_option_large_os_pages, true);

    create_instance<Nucleus>(out);
}
