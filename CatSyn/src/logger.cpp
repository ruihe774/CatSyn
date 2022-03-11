#include <algorithm>

#include <stdio.h>
#include <string.h>

#include <catimpl.h>

#ifdef _WIN32

#include <Windows.h>

static void throw_system_error() {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
}

static bool check_support_ascii_escape() noexcept {
    DWORD mode;
    if (!GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode))
        return false;
    return mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;
}

void write_err(const char* s, size_t n) noexcept {
    // WriteFile always write all if success
    if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, n, nullptr, nullptr))
        throw_system_error();
}

static void set_thread_priority(boost::thread& thread, int priority, bool allow_boost = true) noexcept {
    HANDLE hThread = thread.native_handle();
    SetThreadPriority(hThread, priority);
    SetThreadPriorityBoost(hThread, !allow_boost);
}

#else

#include <unistd.h>

static bool check_support_ascii_escape() noexcept {
    return isatty(2);
}

void write_err(const char* s, size_t n) noexcept {
    // XXX: sync?
    fwrite_unlocked(s, n, 1, stderr);
}

static void set_thread_priority(boost::thread& thread, int priority, bool allow_boost = true) noexcept {
    // TODO: implement this
}

#endif

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

    char buf[1024];
    write_err(buf, std::min(sizeof(buf) - 1, static_cast<size_t>(snprintf(buf, sizeof(buf), "%s%s%s\t%s\n", color,
                                                                          prompt, clear, msg))));
}

static void log_worker(boost::lockfree::queue<uintptr_t, boost::lockfree::capacity<128>>& queue,
                       boost::sync::semaphore& semaphore, ILogSink** sink) {
    bool enable_ascii_escape = check_support_ascii_escape();
    auto f = [=](uintptr_t record) {
        auto level = static_cast<LogLevel>((record & 3u) * 10u);
        auto msg = reinterpret_cast<char*>(record & ~static_cast<uintptr_t>(3));
        if (auto sk = *sink; sk)
            sk->send_log(level, msg);
        else
            log_out(level, msg, enable_ascii_escape);
        operator delete(msg);
    };
    try {
        while (true) {
            boost::this_thread::interruption_point();
            semaphore.wait();
            queue.consume_all(f);
        }
    } catch (boost::thread_interrupted&) {
        queue.consume_all(f);
        throw;
    }
}

Logger::Logger() : filter_level(LogLevel::DEBUG), thread(log_worker, boost::ref(queue), boost::ref(semaphore), sink.addressof()) {
    set_thread_priority(thread, -1, false);
}

Logger::~Logger() {
    thread.interrupt();
    semaphore.post();
    thread.join();
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
    if (!queue.push(reinterpret_cast<uintptr_t>(copied) | static_cast<uintptr_t>(level) / 10))
        log_failed();
    semaphore.post();
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
