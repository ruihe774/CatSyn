#pragma once

#include <atomic>

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

CAT_API ITable* create_table(size_t reserve_capacity = 0);

class IBytes : virtual public IObject {
  protected:
    void* buf;
    size_t len;

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

  protected:
    IBytes(void* buf, size_t len): buf(buf), len(len) {}
};

CAT_API IBytes* create_bytes(const void* data, size_t len);

class IIntegerArray : virtual public IObject {
    virtual size_t get_array(int64_t* out) noexcept = 0;
    virtual size_t get_array(const int64_t* out) const noexcept = 0;

    virtual void set_array(int64_t* in, size_t len, bool extend) = 0;
};

CAT_API IIntegerArray* create_integer_array(size_t reserve_capacity = 0);

class INumberArray : virtual public IObject {
    virtual size_t get_array(double* out) noexcept = 0;
    virtual size_t get_array(const double* out) const noexcept = 0;

    virtual void set_array(double* in, size_t len, bool extend) = 0;
};

CAT_API INumberArray* create_number_array(size_t reserve_capacity = 0);

} // namespace catsyn
