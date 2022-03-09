#include <stdio.h>

#include <Windows.h>
#include <io.h>

#include <catimpl.h>

static bool check_support_ascii_escape() noexcept {
    DWORD mode;
    if (!GetConsoleMode(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))), &mode))
        return false;
    return mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;
}

Logger::Logger() : enable_ascii_escape(check_support_ascii_escape()) {}

void Logger::log(LogLevel level, const char* msg) const noexcept {
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

    fprintf(stderr, "%s%s%s\t%s\n", color, prompt, clear, msg);
}

void Logger::clone(IObject** out) const noexcept {
    not_implemented();
}
