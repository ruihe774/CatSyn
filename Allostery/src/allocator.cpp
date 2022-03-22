#include <new>

#include <snmalloc.h>

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
    return snmalloc::ThreadAlloc::get()->alloc(count);
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
        snmalloc::ThreadAlloc::get()->dealloc(ptr);
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
