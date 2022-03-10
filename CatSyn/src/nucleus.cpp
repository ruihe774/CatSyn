#include <stdexcept>

#include <boost/stacktrace/stacktrace.hpp>

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

void print_stacktrace() {
    auto st = to_string(boost::stacktrace::stacktrace());
#ifdef _WIN32
    st += "Aborted\n";
#endif
    write_err(st.data(), st.size());

    std::set_terminate(nullptr);
    std::terminate();
}

CAT_API void catsyn::create_nucleus(INucleus** out) {
    mi_option_set_enabled_default(mi_option_large_os_pages, true);
    std::set_terminate(print_stacktrace);

    *out = new Nucleus;
    (*out)->add_ref();
}
