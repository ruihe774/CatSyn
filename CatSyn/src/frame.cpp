#include <array>

#include <catimpl.h>

#include <allostery.h>

class Bytes : public Object, virtual public IBytes {
  public:
    Bytes(const void* data, size_t len) noexcept {
        this->buf = operator new(len);
        this->len = len;
        if (data)
            round_copy(this->buf, data, len);
    }

    ~Bytes() override {
        operator delete(this->buf);
    }

    void clone(IObject** out) const noexcept final {
        create_instance<Bytes>(out, this->buf, this->len);
    }

    void realloc(size_t new_size) noexcept final {
        this->buf = re_alloc(this->buf, new_size);
    }
};

class Array : public Bytes, public IArray {
  public:
    Array(const std::type_info* type, const void* data, size_t bytes_count) noexcept : Bytes(data, bytes_count) {
        this->element_type = type;
    }
};

void Nucleus::create_bytes(const void* data, size_t len, IBytes** out) noexcept {
    create_instance<Bytes>(out, data, len);
}

void Nucleus::create_array(const std::type_info* type, const void* data, size_t bytes_count, IArray** out) noexcept {
    create_instance<Array>(out, type, data, bytes_count);
}

class Frame final : public Object, public IFrame, public Shuttle {
    static constexpr unsigned max_plane_count = 3;

    FrameInfo fi;
    std::array<cat_ptr<const IBytes>, max_plane_count> planes;
    std::array<size_t, max_plane_count> strides;
    cat_ptr<const ITable> props;

    void check_idx(unsigned idx) const {
        if (idx >= num_planes(fi.format))
            throw std::out_of_range("plane index out of range");
    }

  public:
    const IBytes* get_plane(unsigned idx) const noexcept final {
        check_idx(idx);
        return planes[idx].get();
    };

    IBytes* get_plane_mut(unsigned idx) noexcept final {
        check_idx(idx);
        auto& plane = planes[idx];
        auto plane_mut = plane.usurp_or_clone();
        plane = plane_mut;
        return plane_mut.get();
    }

    void set_plane(unsigned idx, const IBytes* in, size_t stride) noexcept final {
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

    Frame(Nucleus& nucl, FrameInfo fi, const IBytes** in_planes, const size_t* in_strides,
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
                nucl.create_bytes(nullptr, len, planes[idx].put());
                strides[idx] = stride;
            }
        }
        if (in_props)
            props = in_props;
        else
            nucl.create_table(0, props.put());
    }

    void clone(IObject** out) const noexcept final {
        create_instance<Frame>(out, this->nucl, fi, (const IBytes**)planes.data(), strides.data(), props.get());
    }
};

void Nucleus::create_frame(FrameInfo fi, const IBytes** planes, const size_t* strides, const ITable* props,
                           IFrame** out) noexcept {
    create_instance<Frame>(out, *this, fi, planes, strides, props);
}
