#pragma once

#include <array>
#include <atomic>
#include <new>
#include <utility>

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

class IClone : virtual public IObject {
  public:
    virtual void clone(IObject** out) const = 0;
};

class ILockable : virtual public IObject {
  public:
    virtual void lock() const noexcept = 0;
    virtual bool is_locked() const noexcept = 0;
};

class ITable : virtual public IClone, virtual public ILockable {
  public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    virtual const IObject* get(size_t idx) const = 0;
    virtual IObject* get_mut(size_t idx) = 0;

    virtual void set(size_t idx, IObject* obj, bool insert) = 0;

    virtual void del(size_t idx) = 0;

    virtual size_t get_idx(const char* key) const noexcept = 0;
    virtual const char* get_key(size_t idx) const noexcept = 0;

    virtual void set_key(size_t idx, const char* key) = 0;

    virtual size_t size() const noexcept = 0;
};

class IBytes : virtual public ILockable {
    mutable std::atomic_bool locked{false};

  protected:
    void* buf{nullptr};
    size_t len{0};

  public:
    void* data_mut() noexcept {
        if (is_locked())
            return nullptr;
        else
            return buf;
    }

    const void* data() const noexcept {
        return buf;
    }

    size_t size() const noexcept {
        return len;
    }

    void lock() const noexcept final {
        locked.store(true, std::memory_order_acq_rel);
    }

    bool is_locked() const noexcept final {
        return locked.load(std::memory_order_acquire);
    }
};

class IAlignedBytes : virtual public IBytes {
  public:
    static constexpr std::align_val_t alignment = static_cast<std::align_val_t>(64);
};

class IIntegerArray : virtual public IClone {
  public:
    virtual size_t get_array(int64_t* out) noexcept = 0;
    virtual size_t get_array(const int64_t* out) const noexcept = 0;

    virtual void set_array(int64_t* in, size_t len, bool extend) = 0;
};

class INumberArray : virtual public IClone {
  public:
    virtual size_t get_array(double* out) noexcept = 0;
    virtual size_t get_array(const double* out) const noexcept = 0;

    virtual void set_array(double* in, size_t len, bool extend) = 0;
};

struct FrameInfo;
class IFrame;

class INucleus : virtual public IObject {
  public:
    virtual void create_bytes(const void* data, size_t len, IBytes** out) noexcept = 0;
    virtual void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept = 0;

    virtual void create_frame(FrameInfo fi, const IAlignedBytes** planes, const uintptr_t* strides, const ITable* props,
                              IFrame** out) noexcept = 0;

    virtual void create_table(size_t reserve_capacity, ITable** out) noexcept = 0;
    virtual void create_integer_array(size_t reserve_capacity, IIntegerArray** out) noexcept = 0;
    virtual void create_number_array(size_t reserve_capacity, INumberArray** out) noexcept = 0;
};

enum class SampleType {
    Integer,
    Float,
};

enum class ColorFamily {
    Gray = 1,
    RGB,
    YUV,
};

union FrameFormat {
    uint32_t id;
    struct {
        unsigned height_subsampling : 8;
        unsigned width_subsampling : 8;
        unsigned bits_per_sample : 8;
        SampleType sample_type : 4;
        ColorFamily color_family : 4;
    } detail;

    unsigned bytes_per_sample() const noexcept {
        return (detail.bits_per_sample + 7u) / 8u;
    }
};

struct FrameInfo {
    FrameFormat format;
    unsigned width;
    unsigned height;

    uintptr_t width_bytes() const noexcept {
        return static_cast<uintptr_t>(width) * format.bytes_per_sample();
    }
};

struct FpsFraction {
    unsigned num;
    unsigned den;
};

struct VideoInfo {
    FrameInfo frame_info;
    FpsFraction fps;
    size_t frame_count;
};

class IFrame : virtual public IObject {
  public:
    virtual const IAlignedBytes* get_plane(unsigned idx) const noexcept = 0;
    virtual IAlignedBytes* get_plane_mut(unsigned idx) noexcept = 0;

    virtual void set_plane(unsigned idx, IAlignedBytes* in, uintptr_t stride) noexcept = 0;

    virtual FrameInfo get_frame_info() const noexcept = 0;

    virtual uintptr_t get_stride(unsigned idx) const noexcept = 0;

    unsigned get_plane_width(unsigned idx) const noexcept {
        auto fi = get_frame_info();
        return fi.width >> (idx ? fi.format.detail.width_subsampling : 0u);
    }

    unsigned get_plane_height(unsigned idx) const noexcept {
        auto fi = get_frame_info();
        return fi.height >> (idx ? fi.format.detail.height_subsampling : 0u);
    }

    unsigned get_plane_count() const noexcept {
        return get_frame_info().format.detail.color_family == ColorFamily::Gray ? 1 : 3;
    }

    virtual const ITable* get_frame_props() const noexcept = 0;
    virtual ITable* get_frame_props_mut() noexcept = 0;
    virtual void set_frame_props(ITable* props) noexcept = 0;
};

} // namespace catsyn
