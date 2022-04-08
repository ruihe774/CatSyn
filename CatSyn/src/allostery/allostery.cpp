#include <new>

#ifdef _WIN32
#include <Windows.h>
#include <immintrin.h>
#include <intrin.h>
#endif

#include <allostery.h>

#ifdef _WIN32
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

void* operator new(size_t count) {
    auto ptr = operator new(count, std::nothrow);
    if (ptr) [[likely]]
        return ptr;
    while (!ptr) {
        if (auto new_handler = std::get_new_handler(); new_handler)
            new_handler();
        else
            throw std::bad_alloc();
        ptr = operator new(count, std::nothrow);
    }
    return ptr;
}

void* operator new[](size_t count) {
    return operator new(count);
}

void* operator new(size_t count, const std::nothrow_t& tag) noexcept {
    return alloc(count);
}

void* operator new[](size_t count, const std::nothrow_t& tag) noexcept {
    return operator new(count, tag);
}

void operator delete(void* ptr) noexcept {
    if (ptr) [[likely]]
        dealloc(ptr);
}

void operator delete[](void* ptr) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    return operator delete(ptr);
}

template<size_t size> ALWAYS_INLINE void copy_one(void* __restrict dst, const void* __restrict src) {
    struct Block {
        char data[size];
    };
    auto* d = static_cast<Block*>(dst);
    auto* s = static_cast<const Block*>(src);
    *d = *s;
}

template<size_t size, size_t word> ALWAYS_INLINE void small_copy(void* dst, const void* src) {
    if constexpr (size > 0) {
        if constexpr (size >= word) {
            copy_one<word>(dst, src);
            small_copy<size - word, word>(static_cast<char*>(dst) + word, static_cast<const char*>(src) + word);
        } else {
            small_copy<size, word / 2>(dst, src);
        }
    }
}

template<size_t size, size_t word = size> ALWAYS_INLINE void small_copies(void* dst, const void* src, size_t len) {
    if (len == size) {
        small_copy<size, word>(dst, src);
    }
    if constexpr (size > 0) {
        small_copies<size - 1, word>(dst, src, len);
    }
}

ALWAYS_INLINE void movsb(unsigned char* dst, const unsigned char* src, size_t size) noexcept {
#ifdef _WIN32
    __movsb(dst, src, size);
#else
    asm volatile("rep movsb" : "+S"(src), "+D"(dst), "+c"(size) : : "memory");
#endif
}

void round_copy(void* __restrict dst, const void* __restrict src, size_t size) noexcept {
    if (size < 32)
        small_copies<32>(dst, src, size);
    else if (size <= 256 * 1024)
        movsb(static_cast<unsigned char*>(dst), static_cast<const unsigned char*>(src), size);
    else
        for (size_t i = 0; i < ((size + 31) / 32 + 7) / 8 * 8; ++i) {
            auto m = _mm256_load_si256((const __m256i*)(src) + i);
            _mm256_stream_si256((__m256i*)(dst) + i, m);
        }
}

void* re_alloc(void* ptr, size_t new_size) noexcept {
    auto old_size = alloc_size(ptr);
    if (old_size >= new_size)
        return ptr;
    auto new_ptr = operator new(new_size);
    round_copy(new_ptr, ptr, old_size);
    operator delete(ptr);
    return new_ptr;
}

#ifdef ALLOSTERY_IMPL

#include <atomic>
#include <chrono>
#include <stack>

#include <boost/lockfree/stack.hpp>

#include <mimalloc.h>

#include <tatabox.h>

static class Pool {
  public:
    static constexpr size_t num_size_classes = 16;
    static constexpr size_t size_class_offset = 12;
    static constexpr size_t base = 0x700000000000ull;
    static constexpr size_t bin = 0x2000000000ull;
    static constexpr size_t large_page_min = 0x200000ull;

  private:
    struct Stack : boost::lockfree::stack<void*, boost::lockfree::allocator<mi_stl_allocator<char>>> {
        Stack() noexcept : stack(64) {}
    };

    Stack stacks[num_size_classes];
    std::atomic_size_t cur[num_size_classes];
    HANDLE notification_handle, wait_handle;

    static size_t size_to_size_class(size_t size) noexcept {
        auto size_class = std::bit_width(size - 1) - size_class_offset;
        cond_check(size_class < num_size_classes, "alloc too large");
        return size_class;
    }

    static size_t size_class_to_size(size_t size_class) noexcept {
        return 1 << size_class_offset << size_class;
    }

    static void CALLBACK low_memory(PVOID pl, BOOLEAN) {
        static uint64_t last_time = 0;
        uint64_t cur_time =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (cur_time - last_time > 8) {
            last_time = cur_time;
            format_to_err("MEMORY LOW\n");
            auto self = static_cast<Pool*>(pl);
            std::stack<void*> temp;
            for (size_t size_class = 0; size_class < num_size_classes; ++size_class) {
                auto& stack = self->stacks[size_class];
                stack.consume_all([&temp, size_class](void* p) {
                    VirtualAlloc(p, size_class_to_size(size_class), MEM_RESET, PAGE_READWRITE);
                    temp.push(p);
                });
                while (!temp.empty()) {
                    stack.push(temp.top());
                    temp.pop();
                }
            }
        }
    }

  public:
    Pool() noexcept {
        cond_check(reinterpret_cast<size_t>(VirtualAlloc(reinterpret_cast<void*>(base), bin * num_size_classes,
                                                         MEM_RESERVE, PAGE_READWRITE)) == base,
                   "alloc failed");

        HANDLE hToken;
        TOKEN_PRIVILEGES tp;
        BOOL status;
        OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
        LookupPrivilegeValueW(nullptr, L"SeLockMemoryPrivilege", &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        status = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES) nullptr, 0);
        cond_check(status, "failed to gain privilege");
        CloseHandle(hToken);

        notification_handle = CreateMemoryResourceNotification(LowMemoryResourceNotification);
        RegisterWaitForSingleObject(&wait_handle, notification_handle, low_memory, this, INFINITE, WT_EXECUTEDEFAULT);
    }

    ~Pool() {
        CloseHandle(wait_handle);
        CloseHandle(notification_handle);
        VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE);
    }

    size_t alloc_size(void* ptr) noexcept {
        auto pv = reinterpret_cast<size_t>(ptr);
        return size_class_to_size((pv - base) / bin);
    }

    void* alloc(size_t size) noexcept {
        auto size_class = size_to_size_class(size);
        auto round_size = size_class_to_size(size_class);
        auto& stack = stacks[size_class];
        void* p;
        if (stack.pop(p))
            return p;
        auto offset = cur[size_class].fetch_add(1, std::memory_order_acq_rel);
        offset *= round_size;
        cond_check(offset < bin, "pool exhausted");
        p = reinterpret_cast<void*>(base + bin * size_class + offset);
        cond_check(VirtualAlloc(p, round_size, MEM_COMMIT | (round_size >= large_page_min ? MEM_LARGE_PAGES : 0),
                                PAGE_READWRITE) == p,
                   "alloc failed");
        return p;
    }

    void dealloc(void* ptr) noexcept {
        auto size = alloc_size(ptr);
        auto size_class = size_to_size_class(size);
        auto& stack = stacks[size_class];
        stack.push(ptr);
    }

    bool own_ptr(void* ptr) noexcept {
        auto pv = reinterpret_cast<size_t>(ptr);
        return pv >= base && pv < base + bin * num_size_classes;
    }
} pool;

ALLOSTERY_API void* alloc(size_t size) {
    if (size >= 1 << Pool::size_class_offset)
        return pool.alloc(size);
    else
        return mi_malloc(size);
}

ALLOSTERY_API void dealloc(void* ptr) {
    if (pool.own_ptr(ptr))
        pool.dealloc(ptr);
    else
        mi_free(ptr);
}

ALLOSTERY_API size_t alloc_size(void* ptr) {
    if (pool.own_ptr(ptr))
        return pool.alloc_size(ptr);
    else
        return mi_malloc_size(ptr);
}

#endif
