#include <filesystem>
#include <system_error>

#include <unistd.h>

thread_local char fmt_buf[4096] __attribute__((weak));

[[noreturn]] inline void unreachable() noexcept {
    __builtin_unreachable();
}

inline void throw_system_error() {
    throw std::system_error(errno, std::system_category());
}

inline void write_err(const char* msg, size_t size) noexcept {
    system_call_check(write(2, msg, size) == static_cast<ssize_t>(size));
}

template<typename T> void set_thread_priority(T& thread, int priority, bool allow_boost) noexcept {
    // STUB
}

class SharedLibrary {
  public:
    explicit SharedLibrary(const std::filesystem::path& path) {
        // STUB
    }

    template<typename F> std::enable_if_t<std::is_function_v<F>, std::add_pointer_t<F>> get_function(const char* name) {
        // STUB
        return nullptr;
    }

    ~SharedLibrary() {
        // STUB
    }

    static std::filesystem::path get_current_module_path() noexcept {
        // STUB
        return "";
    }

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary(SharedLibrary&& other) noexcept {
        // STUB
    }
};
