#include <new>

#include <snmalloc.h>

#include <allostery.h>

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
    return operator new(snmalloc::aligned_size(static_cast<size_t>(al), count));
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
    return operator new(snmalloc::aligned_size(static_cast<size_t>(al), count), tag);
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

size_t round_size(size_t size) noexcept {
    return snmalloc::round_size(size);
}

void round_copy(void* __restrict dst, const void* __restrict src, size_t size) noexcept {
    if (reinterpret_cast<uintptr_t>(dst) % 32 || reinterpret_cast<uintptr_t>(src) % 32) [[unlikely]]
        memcpy(dst, src, size);
    else if (size < 32)
        for (size_t i = 0; i < size; ++i)
            static_cast<char*>(dst)[i] = static_cast<const char*>(src)[i];
    else if (size <= 256 * 1024)
        for (size_t i = 0; i < (size + 31) / 32; ++i) {
            auto m = _mm256_load_si256((const __m256i*)(src) + i);
            _mm256_store_si256((__m256i*)(dst) + i, m);
        }
    else
        for (size_t i = 0; i < (size + 31) / 32; ++i) {
            auto m = _mm256_load_si256((const __m256i*)(src) + i);
            _mm256_stream_si256((__m256i*)(dst) + i, m);
        }
}

void* re_alloc(void* ptr, size_t new_size) noexcept {
    auto old_size = alloc_size(ptr);
    if (round_size(old_size) >= new_size)
        return ptr;
    auto new_ptr = operator new(new_size);
    round_copy(new_ptr, ptr, old_size);
    operator delete(ptr);
    return new_ptr;
}

#ifdef ALLOSTERY_IMPL
ALLOSTERY_API void* alloc(size_t size) {
    return snmalloc::ThreadAlloc::get()->alloc(size);
}

ALLOSTERY_API void dealloc(void* ptr) {
    return snmalloc::ThreadAlloc::get()->dealloc(ptr);
}

ALLOSTERY_API size_t alloc_size(void* ptr) {
    return snmalloc::ThreadAlloc::get()->alloc_size(ptr);
}
#endif
