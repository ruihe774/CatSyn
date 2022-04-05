#include <new>

#ifdef _WIN32
#include <intrin.h>
#include <immintrin.h>
#endif

#include <mimalloc.h>

#include <allostery.h>
#include <tatabox.h>

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

void* operator new(size_t count, std::align_val_t al) {
    not_implemented();
}

void* operator new[](size_t count, std::align_val_t al) {
    return operator new(count, al);
}

void* operator new(size_t count, const std::nothrow_t& tag) noexcept {
    return alloc(count);
}

void* operator new[](size_t count, const std::nothrow_t& tag) noexcept {
    return operator new(count, tag);
}

void* operator new(size_t count, std::align_val_t al, const std::nothrow_t& tag) noexcept {
    not_implemented();
}

void* operator new[](size_t count, std::align_val_t al, const std::nothrow_t& tag) noexcept {
    return operator new(count, al, tag);
}

void operator delete(void* ptr) noexcept {
    if (ptr) [[likely]]
        dealloc(ptr);
}

void operator delete[](void* ptr) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t al) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, std::align_val_t al) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, size_t, std::align_val_t) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, size_t, std::align_val_t) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    return operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    return operator delete(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
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
ALLOSTERY_API void* alloc(size_t size) {
    auto alignment = std::hardware_destructive_interference_size;
    if (size < alignment)
        return mi_malloc_small(size);
    else
        return mi_aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}

ALLOSTERY_API void dealloc(void* ptr) {
    mi_free(ptr);
}

ALLOSTERY_API size_t alloc_size(void* ptr) {
    return mi_malloc_size(ptr);
}
#endif
