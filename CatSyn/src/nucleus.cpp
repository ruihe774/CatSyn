#include <catimpl.h>

NucleusConfig create_default_config(NucleusConfig tmpl) noexcept {
    tmpl.thread_count = tmpl.thread_count ? tmpl.thread_count : std::thread::hardware_concurrency();
    tmpl.mem_hint_mb = tmpl.mem_hint_mb ? tmpl.mem_hint_mb : 4096;
    return tmpl;
}

Shuttle::Shuttle(Nucleus& nucl) noexcept : nucl(nucl) {}

Nucleus::Nucleus() {
    cat_ptr<ITable> t;
    create_table(0, t.put());
    finders = t.query<Table>();

    create_table(1, t.put());
    ribosomes = t.query<Table>();

    cat_ptr<IRibosome> csv1;
    create_catsyn_v1_ribosome(csv1.put());
    ribosomes->set(ITable::npos, csv1.get(), csv1->get_identifier());

    create_table(0, t.put());
    enzymes = t.query<Table>();
}

IFactory* Nucleus::get_factory() noexcept {
    return this;
}

ILogger* Nucleus::get_logger() noexcept {
    return &logger;
}

ITable* Nucleus::get_enzyme_finders() noexcept {
    return finders.get();
}

ITable* Nucleus::get_ribosomes() noexcept {
    return ribosomes.get();
}

ITable* Nucleus::get_enzymes() noexcept {
    return enzymes.get();
}

void Nucleus::set_config(NucleusConfig cfg) noexcept {
    cond_check(!is_reacting(), "changing config is not allowed during reaction");
    config = create_default_config(cfg);
}

NucleusConfig Nucleus::get_config() const noexcept {
    return config;
}

CAT_API Version catsyn::get_version() noexcept {
    return version;
}

CAT_API void catsyn::create_nucleus(INucleus** out) noexcept {
    create_instance<Nucleus>(out);
}
