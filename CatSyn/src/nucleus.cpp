#include <stdexcept>

#include <mimalloc.h>

#include <catimpl.h>

void Nucleus::clone(IObject** out) const noexcept {
    not_implemented();
}

IFactory* Nucleus::get_factory() noexcept {
    return this;
}

ILogger* Nucleus::get_logger() noexcept {
    return &logger;
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
