#include <cstring>

#include <catimpl.h>

static void log_out(LogLevel level, const char* msg) noexcept {
    const char *prompt;
    switch (level) {
    case LogLevel::DEBUG:
        prompt = "🏗️";
        break;
    case LogLevel::INFO:
        prompt = "ℹ️";
        break;
    case LogLevel::WARNING:
        prompt = "⚠️";
        break;
    }

    format_to_err("{}\t{}\n", prompt, msg);
}

static void log_worker(SCQueue<uintptr_t>& queue, ILogSink* const& sink) {
    queue.stream([&](uintptr_t record) {
        auto level = static_cast<LogLevel>((record & 3u) * 10u);
        auto msg = reinterpret_cast<char*>(record & ~static_cast<uintptr_t>(3));
        if (sink)
            sink->send_log(level, msg);
        else
            log_out(level, msg);
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

void Logger::log(LogLevel level, const char* msg) const noexcept {
    if (level < filter_level)
        return;
    auto msg_len = std::strlen(msg);
    auto copied = static_cast<char*>(operator new(msg_len + 1));
    std::memcpy(copied, msg, msg_len + 1);
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
