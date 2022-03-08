#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>

#include <string.h>

#include <catimpl.h>

using namespace catsyn;

class AllocHolder {
  protected:
    AllocStat& alloc_stat;
    explicit AllocHolder(AllocStat& stat) noexcept : alloc_stat(stat) {}
};

class Bytes final : public Object, public IBytes, public AllocHolder {
  public:
    Bytes(AllocStat& alloc_stat, const void* data, size_t len) noexcept : AllocHolder(alloc_stat) {
        auto p = operator new(len);
        if (data)
            memcpy(p, data, len);
        this->buf = p;
        this->len = len;
        alloc_stat.alloc(len);
    }

    ~Bytes() final {
        operator delete(this->buf);
        alloc_stat.free(len);
    }
};

class AlignedBytes final : public Object, public IAlignedBytes, public AllocHolder {
  public:
    AlignedBytes(AllocStat& alloc_stat, const void* data, size_t len) noexcept : AllocHolder(alloc_stat) {
        auto p = operator new(len, alignment);
        if (data)
            memcpy(p, data, len);
        this->buf = p;
        this->len = len;
        alloc_stat.alloc(len);
    }

    ~AlignedBytes() final {
        operator delete(this->buf, alignment);
        alloc_stat.free(len);
    }
};

void Nucleus::create_bytes(const void* data, size_t len, IBytes** out) noexcept {
    *out = new Bytes(alloc_stat, data, len);
    (*out)->add_ref();
}

void Nucleus::create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept {
    *out = new AlignedBytes(alloc_stat, data, len);
    (*out)->add_ref();
}

void AllocStat::alloc(size_t size) noexcept {
    current.fetch_add(size, std::memory_order_relaxed);
}

void AllocStat::free(size_t size) noexcept {
    current.fetch_sub(size, std::memory_order_relaxed);
}

size_t AllocStat::get_current() const noexcept {
    return current.load(std::memory_order_acq_rel);
}

class Frame final : public Object, public IFrame {
    static constexpr unsigned max_plane_count = 3;

    INucleus* nucl;
    FrameInfo fi;
    std::array<cat_ptr<IAlignedBytes>, max_plane_count> planes;
    std::array<uintptr_t, max_plane_count> strides;
    cat_ptr<ITable> props;

    void check_idx(unsigned idx) const {
        if (idx >= this->get_plane_count())
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
        if (plane->is_locked()) {
            cat_ptr<IAlignedBytes> new_plane;
            nucl->create_aligned_bytes(plane->data(), plane->size(), new_plane.put());
            plane.swap(new_plane);
        }
        return plane.get();
    }

    void set_plane(unsigned idx, IAlignedBytes* in, uintptr_t stride) noexcept final {
        check_idx(idx);
        planes[idx] = in;
        strides[idx] = stride;
    }

    FrameInfo get_frame_info() const noexcept final {
        return fi;
    }

    uintptr_t get_stride(unsigned idx) const noexcept final {
        check_idx(idx);
        return strides[idx];
    }

    const ITable* get_frame_props() const noexcept final {
        return props.get();
    }

    ITable* get_frame_props_mut() noexcept final {
        if (props->is_locked()) {
            cat_ptr<IObject> new_props;
            props->clone(new_props.put());
            props = new_props.query<ITable>();
        }
        return props.get();
    }

    void set_frame_props(ITable* new_props) noexcept final {
        props = new_props;
    }

    Frame(INucleus* nucl, FrameInfo fi, const IAlignedBytes** in_planes, const uintptr_t* in_strides,
          const ITable* in_props) noexcept
        : nucl(nucl), fi(fi) {
        auto count = this->get_plane_count();
        for (unsigned idx = 0; idx < count; ++idx) {
            if (in_planes && in_planes[idx]) {
                auto in_plane = in_planes[idx];
                in_plane->lock();
                planes[idx] = const_cast<IAlignedBytes*>(in_plane);
                strides[idx] = in_strides[idx];
            } else {
                auto align = static_cast<uintptr_t>(IAlignedBytes::alignment);
                auto stride = (fi.width_bytes() + align - 1u) / align * align;
                size_t len = stride * fi.height;
                nucl->create_aligned_bytes(nullptr, len, planes[idx].put());
                strides[idx] = stride;
            }
        }
        if (in_props) {
            in_props->lock();
            props = const_cast<ITable*>(in_props);
        } else {
            nucl->create_table(props->size(), props.put());
        }
    }
};

void Nucleus::create_frame(FrameInfo fi, const IAlignedBytes** planes, const uintptr_t* strides, const ITable* props,
                           IFrame** out) noexcept {
    *out = new Frame(this, fi, planes, strides, props);
}
