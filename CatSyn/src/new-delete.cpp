#include <snmalloc.h>

using namespace snmalloc;

void* operator new(std::size_t n) noexcept(false) {
    return ThreadAlloc::get()->alloc(n);
}

void* operator new[](std::size_t n) noexcept(false) {
    return ThreadAlloc::get()->alloc(n);
}

void* operator new(std::size_t n, const std::nothrow_t& tag) noexcept {
    return ThreadAlloc::get()->alloc(n);
}

void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept {
    return ThreadAlloc::get()->alloc(n);
}

void operator delete(void* p) noexcept {
    ThreadAlloc::get()->dealloc(p);
}

void operator delete(void* p, std::size_t n) noexcept {
    if (p == nullptr)
        return;
    ThreadAlloc::get()->dealloc(p, n);
}

void operator delete[](void* p) noexcept {
    ThreadAlloc::get()->dealloc(p);
}

void operator delete[](void* p, std::size_t n) noexcept {
    if (p == nullptr)
        return;
    ThreadAlloc::get()->dealloc(p, n);
}
