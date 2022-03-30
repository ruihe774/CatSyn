#pragma once

#include <exception>
#include <type_traits>
#include <thread>

#include <fmt/format.h>

template<typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<std::is_base_of_v<std::exception, T>>> : fmt::formatter<const char*> {
    template<typename FormatContext> constexpr auto format(const std::exception& exc, FormatContext& ctx) const {
        return fmt::formatter<const char*>::format(exc.what(), ctx);
    }
};

[[noreturn]] inline void unreachable() noexcept;

inline void cond_check(bool cond, const char* msg) noexcept;
inline void buffer_size_check(bool cond) noexcept;

extern thread_local char fmt_buf[4096];

template<typename... Args> size_t format_c_impl(fmt::format_string<Args...> fmt, Args&&... args) noexcept {
    auto size = fmt::format_to_n(fmt_buf, sizeof(fmt_buf) - 1, std::move(fmt), std::forward<Args>(args)...).size;
    buffer_size_check(size < sizeof(fmt_buf));
    fmt_buf[size] = 0;
    return size;
}

template<typename... Args> const char* format_c(fmt::format_string<Args...> fmt, Args&&... args) noexcept {
    format_c_impl(std::move(fmt), std::forward<Args>(args)...);
    return fmt_buf;
}

inline void write_err(const char* msg, size_t size) noexcept;

template<typename... Args> void format_to_err(fmt::format_string<Args...> fmt, Args&&... args) noexcept {
    write_err(fmt_buf, format_c_impl(std::move(fmt), std::forward<Args>(args)...));
}

[[noreturn]] inline void terminate_with_msg(const char* msg) noexcept {
    format_to_err("{}\n", msg);
    std::terminate();
}

inline void cond_check(bool cond, const char* msg) noexcept {
    if (!cond) [[unlikely]]
        terminate_with_msg(msg);
}

inline void buffer_size_check(bool cond) noexcept {
    cond_check(cond, "insufficient buffer");
}

[[noreturn]] inline void not_implemented() noexcept {
    terminate_with_msg("not implemented");
}

[[noreturn]] inline void throw_system_error();

inline void system_call_check(bool cond) {
    if (!cond) [[unlikely]]
        throw_system_error();
}

template<typename T> void set_thread_priority(T& thread, int priority, bool allow_boost = true) noexcept;

class SharedLibrary;

#ifdef _WIN32
#include <pal/windows.h>
#else
#include <pal/posix.h>
#endif
