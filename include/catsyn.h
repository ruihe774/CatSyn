#pragma once

#include <atomic>
#include <new>

#ifdef CAT_IMPL
#define CAT_API __declspec(dllexport)
#else
#define CAT_API __declspec(dllimport)
#endif

namespace catsyn {

class IObject {
    mutable std::atomic_size_t refcount{0};

  public:
    size_t add_ref() const noexcept {
        return refcount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    size_t release() const noexcept {
        auto rc = refcount.fetch_sub(1, std::memory_order_release) - 1;
        if (rc == 0) {
            std::atomic_thread_fence(std::memory_order_acquire);
            const_cast<IObject*>(this)->drop();
        }
        return rc;
    }

    virtual ~IObject() = default;

  protected:
    virtual void drop() noexcept = 0;
};

class ITable : virtual public IObject {
  public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    virtual IObject* get(size_t idx) = 0;
    virtual const IObject* get(size_t idx) const = 0;

    virtual void set(size_t idx, IObject* obj) = 0;

    virtual void del(size_t idx) = 0;

    virtual size_t get_idx(const char* key) const noexcept = 0;
    virtual const char* get_key(size_t idx) const noexcept = 0;

    virtual void set_key(size_t idx, const char* key) = 0;

    virtual size_t size() const noexcept = 0;
};

class IBytes : virtual public IObject {
  protected:
    void* buf{nullptr};
    size_t len{0};

  public:
    void* data() noexcept {
        return buf;
    }

    const void* data() const noexcept {
        return buf;
    }

    size_t size() const noexcept {
        return len;
    }
};

class IAlignedBytes : virtual public IBytes {
  public:
    static constexpr std::align_val_t alignment = static_cast<std::align_val_t>(64);
};

class IIntegerArray : virtual public IObject {
  public:
    virtual size_t get_array(int64_t* out) noexcept = 0;
    virtual size_t get_array(const int64_t* out) const noexcept = 0;

    virtual void set_array(int64_t* in, size_t len, bool extend) = 0;
};

class INumberArray : virtual public IObject {
  public:
    virtual size_t get_array(double* out) noexcept = 0;
    virtual size_t get_array(const double* out) const noexcept = 0;

    virtual void set_array(double* in, size_t len, bool extend) = 0;
};

class INucleus : virtual public IObject {
  public:
    virtual void create_bytes(const void* data, size_t len, IBytes** out) noexcept = 0;
    virtual void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept = 0;
};

} // namespace catsyn
