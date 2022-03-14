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

#ifdef _WIN32

Semaphore::Semaphore(unsigned initial, unsigned max) : h(nullptr) {
    if (max == 0)
        max = LONG_MAX;
    h = CreateSemaphoreW(nullptr, static_cast<LONG>(initial), static_cast<LONG>(max), nullptr);
    if (!h)
        throw_system_error();
}

Semaphore::~Semaphore() {
    if (h)
        CloseHandle(h);
}

void Semaphore::acquire() {
    if (WaitForSingleObject(h, INFINITE))
        throw_system_error();
}

void Semaphore::release() {
    ReleaseSemaphore(h, 1, nullptr);
}

#endif
