#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>

#include <string.h>

#include <mimalloc.h>

#include <catimpl.h>

class Bytes : public Object, virtual public IBytes, public Shuttle {
  public:
    Bytes(Nucleus& nucl, const void* data, size_t len) noexcept : Shuttle(nucl) {
        this->buf = mi_new(len);
        this->len = len;
        this->nucl.alloc_stat.alloc(len);
        if (data)
            memcpy(this->buf, data, len);
    }

    ~Bytes() override {
        mi_free(this->buf);
        this->nucl.alloc_stat.free(len);
    }

    void clone(IObject** out) const noexcept final {
        create_instance<Bytes>(out, this->nucl, this->buf, this->len);
    }

    void realloc(size_t new_size) noexcept final {
        this->buf = mi_new_realloc(this->buf, new_size);
        this->len = new_size;
    }
};

class AlignedBytes final : public Object, public IAlignedBytes, public Shuttle {
  public:
    AlignedBytes(Nucleus& nucl, const void* data, size_t len) noexcept : Shuttle(nucl) {
        this->buf = mi_new_aligned(len, static_cast<size_t>(alignment));
        this->len = len;
        this->nucl.alloc_stat.alloc(len);
        if (data)
            memcpy(this->buf, data, len);
    }

    ~AlignedBytes() final {
        mi_free_aligned(this->buf, static_cast<size_t>(alignment));
        this->nucl.alloc_stat.free(len);
    }

    void clone(IObject** out) const noexcept final {
        create_instance<AlignedBytes>(out, this->nucl, this->buf, this->len);
    }

    void realloc(size_t new_size) noexcept final {
        not_implemented();
    }
};

class NumberArray final : public Bytes, public INumberArray {
  public:
    NumberArray(Nucleus& nucl, SampleType sample_type, const void* data, size_t len): Bytes(nucl, data, len) {
        this->sample_type = sample_type;
    }
};

void Nucleus::create_bytes(const void* data, size_t len, IBytes** out) noexcept {
    create_instance<Bytes>(out, *this, data, len);
}

void Nucleus::create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept {
    create_instance<AlignedBytes>(out, *this, data, len);
}

void Nucleus::create_number_array(SampleType sample_type, const void* data, size_t len, INumberArray** out) noexcept {
    create_instance<NumberArray>(out, *this, sample_type, data, len);
}

void Nucleus::AllocStat::alloc(size_t size) noexcept {
    current.fetch_add(size, std::memory_order_relaxed);
}

void Nucleus::AllocStat::free(size_t size) noexcept {
    current.fetch_sub(size, std::memory_order_relaxed);
}

size_t Nucleus::AllocStat::get_current() const noexcept {
    // precision is not important
    return current.load(std::memory_order_relaxed);
}

class Frame final : public Object, public IFrame, public Shuttle {
    static constexpr unsigned max_plane_count = 3;

    FrameInfo fi;
    std::array<cat_ptr<const IAlignedBytes>, max_plane_count> planes;
    std::array<size_t, max_plane_count> strides;
    cat_ptr<const ITable> props;

    void check_idx(unsigned idx) const {
        if (idx >= num_planes(fi.format))
            throw std::out_of_range("plane index out of range");
    }

  public:
    const IAlignedBytes* get_plane(unsigned idx) const noexcept final {
        check_idx(idx);
        return planes[idx].get();
    };

    IAlignedBytes* get_plane_mut(unsigned idx) noexcept final {
        check_idx(idx);
        auto& plane = planes[idx];
        auto plane_mut = plane.usurp_or_clone();
        plane = plane_mut;
        return plane_mut.get();
    }

    void set_plane(unsigned idx, const IAlignedBytes* in, size_t stride) noexcept final {
        check_idx(idx);
        planes[idx] = in;
        strides[idx] = stride;
    }

    FrameInfo get_frame_info() const noexcept final {
        return fi;
    }

    size_t get_stride(unsigned idx) const noexcept final {
        check_idx(idx);
        return strides[idx];
    }

    const ITable* get_frame_props() const noexcept final {
        return props.get();
    }

    ITable* get_frame_props_mut() noexcept final {
        auto props_mut = props.usurp_or_clone();
        props = props_mut;
        return props_mut.get();
    }

    void set_frame_props(const ITable* new_props) noexcept final {
        props = new_props;
    }

    Frame(Nucleus& nucl, FrameInfo fi, const IAlignedBytes** in_planes, const size_t* in_strides,
          const ITable* in_props) noexcept
        : Shuttle(nucl), fi(fi) {
        auto count = num_planes(fi.format);
        for (unsigned idx = 0; idx < count; ++idx) {
            if (in_planes && in_planes[idx]) {
                planes[idx] = in_planes[idx];
                strides[idx] = in_strides[idx];
            } else {
                auto stride = default_stride(fi, idx);
                size_t len = stride * fi.height;
                nucl.create_aligned_bytes(nullptr, len, planes[idx].put());
                strides[idx] = stride;
            }
        }
        if (in_props)
            props = in_props;
        else
            nucl.create_table(0, props.put());
    }

    void clone(IObject** out) const noexcept final {
        create_instance<Frame>(out, this->nucl, fi, (const IAlignedBytes**)planes.data(), strides.data(), props.get());
    }
};

void Nucleus::create_frame(FrameInfo fi, const IAlignedBytes** planes, const size_t* strides, const ITable* props,
                           IFrame** out) noexcept {
    create_instance<Frame>(out, *this, fi, planes, strides, props);
}
