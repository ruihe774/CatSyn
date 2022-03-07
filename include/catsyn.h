#pragma once

#include <atomic>

namespace catsyn {
class Object {
    mutable std::atomic_uint32_t refcount;

  public:
    uint32_t add_ref() const noexcept {
        return refcount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t release() const noexcept {
        auto rc = refcount.fetch_sub(1, std::memory_order_release) - 1;
        if (rc == 0) {
            std::atomic_thread_fence(std::memory_order_acquire);
            const_cast<Object*>(this)->drop();
        }
        return rc;
    }

    virtual ~Object() = default;

  protected:
    virtual void drop() noexcept = 0;
};
} // namespace catsyn
