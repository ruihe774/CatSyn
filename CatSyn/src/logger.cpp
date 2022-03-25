#include <system_error>

#include <string.h>

#include <catimpl.h>

#include <Windows.h>

[[noreturn]] void throw_system_error() {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
}

static bool check_support_ascii_escape() noexcept {
    DWORD mode;
    if (!GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode))
        return false;
    return mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;
}

static size_t u2w(const char* s, const size_t len, const wchar_t** out) noexcept {
    thread_local wchar_t wbuf[2048];
    auto wlen = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(len), wbuf, sizeof(wbuf));
    if (wlen == 0)
        insufficient_buffer();
    *out = &wbuf[0];
    return wlen;
}

static void write_err(const char* s, size_t n) noexcept {
    static int is_terminal = -1;
    if (is_terminal == -1) {
        DWORD mode;
        is_terminal = GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode);
    }
    if (is_terminal) {
        const wchar_t* ws;
        WriteConsoleW(GetStdHandle(STD_ERROR_HANDLE), ws, u2w(s, n, &ws), nullptr, nullptr);
    } else {
        // WriteFile always write all if success
        if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, n, nullptr, nullptr))
            throw_system_error();
    }
}

void set_thread_priority(std::jthread& thread, int priority, bool allow_boost) noexcept {
    HANDLE hThread = thread.native_handle();
    SetThreadPriority(hThread, priority);
    SetThreadPriorityBoost(hThread, !allow_boost);
}

static void log_out(LogLevel level, const char* msg, bool enable_ascii_escape) noexcept {
    const char *prompt, *color, *clear;
    switch (level) {
    case LogLevel::DEBUG:
        prompt = "DEBUG";
        color = "\x1b[34m";
        break;
    case LogLevel::INFO:
        prompt = "INFO";
        color = "\x1b[36m";
        break;
    case LogLevel::WARNING:
        prompt = "WARNING";
        color = "\x1b[33m";
        break;
    }
    if (enable_ascii_escape) {
        clear = "\x1b[0m";
    } else {
        color = "";
        clear = "";
    }

    char buf[4096];
    write_err(buf, fmt::format_to_n(buf, sizeof(buf), "{}{}{}\t{}\n", color, prompt, clear, msg).out - buf);
}

static void log_worker(SCQueue<uintptr_t>& queue, ILogSink* const& sink) {
    bool enable_ascii_escape = check_support_ascii_escape();
    queue.stream([&](uintptr_t record) {
        auto level = static_cast<LogLevel>((record & 3u) * 10u);
        auto msg = reinterpret_cast<char*>(record & ~static_cast<uintptr_t>(3));
        if (sink)
            sink->send_log(level, msg);
        else
            log_out(level, msg, enable_ascii_escape);
        operator delete(msg);
    });
}

Logger::Logger()
    : filter_level(LogLevel::DEBUG),
      thread(log_worker, std::ref(queue), std::cref(*sink.addressof())) {
    set_thread_priority(thread, -1, false);
}

Logger::~Logger() {
    queue.request_stop();
}

[[noreturn]] static void log_failed() {
    throw std::runtime_error("log failed");
}

void Logger::log(LogLevel level, const char* msg) const noexcept {
    if (level < filter_level)
        return;
    auto msg_len = strlen(msg);
    auto copied = static_cast<char*>(operator new(msg_len + 1));
    memcpy(copied, msg, msg_len + 1);
    queue.push(reinterpret_cast<uintptr_t>(copied) | static_cast<uintptr_t>(level) / 10);
}

void Logger::set_level(LogLevel level) noexcept {
    filter_level = level;
}

void Logger::set_sink(ILogSink* in) noexcept {
    sink = in;
}

void Logger::clone(IObject** out) const noexcept {
    not_implemented();
}
