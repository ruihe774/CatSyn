#include <filesystem>
#include <system_error>

#include <Windows.h>

__declspec(selectany) thread_local char fmt_buf[4096];

[[noreturn]] inline void unreachable() noexcept {
    __assume(false);
}

inline void throw_system_error() {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
}

inline size_t u2w(const char* s, const size_t len, const wchar_t** out) noexcept {
    thread_local wchar_t wbuf[2048];
    auto wlen = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(len), wbuf, sizeof(wbuf));
    buffer_size_check(wlen);
    *out = &wbuf[0];
    return wlen;
}

inline void write_err(const char* msg, size_t size) noexcept {
    static int is_terminal = -1;
    if (is_terminal == -1) [[unlikely]] {
        DWORD mode;
        is_terminal = GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode);
    }
    if (is_terminal) {
        const wchar_t* ws;
        WriteConsoleW(GetStdHandle(STD_ERROR_HANDLE), ws, u2w(msg, size, &ws), nullptr, nullptr);
    } else {
        // WriteFile always write all if success
        system_call_check(WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, size, nullptr, nullptr));
    }
}

template<typename T> void set_thread_priority(T& thread, int priority, bool allow_boost) noexcept {
    HANDLE hThread = thread.native_handle();
    SetThreadPriority(hThread, priority);
    SetThreadPriorityBoost(hThread, !allow_boost);
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

class SharedLibrary {
    HMODULE mod;

  public:
    explicit SharedLibrary(const std::filesystem::path& path)
    : mod(LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)) {
        system_call_check(mod);
    }

    template<typename F> std::enable_if_t<std::is_function_v<F>, std::add_pointer_t<F>> get_function(const char* name) {
        auto func = reinterpret_cast<std::add_pointer_t<F>>(GetProcAddress(mod, name));
        system_call_check(func);
        return func;
    }

    ~SharedLibrary() {
        FreeLibrary(mod);
    }

    static std::filesystem::path get_current_module_path() noexcept {
        auto current_module = reinterpret_cast<HMODULE>(&__ImageBase);
        wchar_t wbuf[2048];
        GetModuleFileNameW(current_module, wbuf, sizeof(wbuf));
        buffer_size_check(GetLastError() != ERROR_INSUFFICIENT_BUFFER);
        return wbuf;
    }
};
