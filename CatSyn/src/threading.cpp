#include <exception>

#include <boost/stacktrace/stacktrace.hpp>

#include <catimpl.h>

void terminate_with_stacktrace() {
    auto st = to_string(boost::stacktrace::stacktrace());
#ifdef _WIN32
    st += "Aborted\n";
#endif
    write_err(st.data(), st.size());

    std::set_terminate(nullptr);
    std::terminate();
}

void thread_init() noexcept {
    std::set_terminate(terminate_with_stacktrace);
}
