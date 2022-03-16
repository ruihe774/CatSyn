#include <boost/stacktrace/stacktrace.hpp>

#include <catimpl.h>

#ifdef _WIN32

namespace cxxexcept {
struct type_info {
    const void* vtable;
    char* name;
    char mangled[128];
};

struct this_ptr_offsets {
    int this_offset;
    int vbase_descr;
    int vbase_offset;
};

struct cxx_type_info {
    unsigned flags;
    unsigned type_info;
    struct this_ptr_offsets offsets;
    unsigned size;
    unsigned copy_ctor;
};

struct cxx_type_info_table {
    unsigned count;
    unsigned info[3];
};

struct cxx_exception_type {
    unsigned flags;
    unsigned destructor;
    unsigned custom_handler;
    unsigned type_info_table;
};

static const char* what(const EXCEPTION_RECORD* rec, const char** type_name) {
    if (rec->ExceptionCode != 0xe06d7363)
        return NULL;
    if (rec->NumberParameters != 4)
        return NULL;
    UINT_PTR magic = rec->ExceptionInformation[0];
    if (magic < 0x19930520 || magic > 0x19930522)
        return NULL;
    ULONG_PTR objptr = rec->ExceptionInformation[1];
    struct cxx_exception_type* info = (struct cxx_exception_type*)rec->ExceptionInformation[2];
    ULONG_PTR base = rec->ExceptionInformation[3];
    struct cxx_type_info_table* ti_table = (struct cxx_type_info_table*)(base + info->type_info_table);
    *type_name = NULL;
    for (unsigned i = 0; i < ti_table->count; ++i) {
        struct cxx_type_info* ti_item = (struct cxx_type_info*)(base + ti_table->info[i]);
        struct type_info* ti = (struct type_info*)(base + ti_item->type_info);
        void* object = (void*)(objptr + ti_item->offsets.this_offset);
        if (!*type_name)
            *type_name = ti->mangled + 1;
        if (strcmp(ti->mangled + 1, "?AVexception@std@@") == 0)
            return ((char* (*)(void*))(*(*(void***)object + 1)))(object);
    }
    return NULL;
}
} // namespace cxxexcept

#endif

void terminate_with_stacktrace() {
    auto st = to_string(boost::stacktrace::stacktrace());
#ifdef _WIN32
    auto excptr = std::current_exception();
    auto rec = reinterpret_cast<EXCEPTION_RECORD*&>(excptr);
    const char* type_name;
    if (auto msg = cxxexcept::what(rec, &type_name); msg) {
        fmt::format_to(std::back_inserter(st), "terminate called after throwing an instance of '{}'\n  what():  {}\n",
                       type_name, msg);
    }
    st += "Aborted\n";
#endif
    write_err(st.data(), st.size());

#ifdef _WIN32
    abort();
#else
    std::set_terminate(nullptr);
    std::terminate();
#endif
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
