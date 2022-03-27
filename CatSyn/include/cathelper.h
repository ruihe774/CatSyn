#pragma once

#include <new>
#include <type_traits>

#include <catsyn.h>

namespace catsyn {

template<typename T> class cat_ptr {
    T* m_ptr;

    typedef std::remove_const_t<T> mutable_element_type;

  public:
    typedef T element_type;
    typedef T* pointer;

    cat_ptr() noexcept : m_ptr(nullptr) {}

    cat_ptr(std::nullptr_t) noexcept : cat_ptr() {}

    cat_ptr(pointer ptr, bool add_ref = true) noexcept : m_ptr(ptr) {
        if (m_ptr && add_ref)
            m_ptr->add_ref();
    }

    cat_ptr(const cat_ptr& other) noexcept : cat_ptr(other.get()) {}

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, pointer>>>
    cat_ptr(const cat_ptr<U>& other) noexcept : cat_ptr(static_cast<pointer>(other.get())) {}

    cat_ptr(cat_ptr&& other) noexcept : m_ptr(other.detach()) {}

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, pointer>>>
    cat_ptr(cat_ptr<U>&& other) noexcept : m_ptr(other.detach()) {}

    ~cat_ptr() noexcept {
        if (m_ptr)
            m_ptr->release();
    }

    cat_ptr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    cat_ptr& operator=(pointer other) noexcept {
        auto ptr = m_ptr;
        m_ptr = other;
        if (m_ptr)
            m_ptr->add_ref();
        if (ptr)
            ptr->release();
        return *this;
    }

    cat_ptr& operator=(const cat_ptr& other) noexcept {
        if (this != &other)
            operator=(other.get());
        return *this;
    }

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, pointer>>>
    cat_ptr& operator=(const cat_ptr<U>& other) noexcept {
        operator=(static_cast<pointer>(other.get()));
        return *this;
    }

    cat_ptr& operator=(cat_ptr&& other) noexcept {
        attach(other.detach());
        return *this;
    }

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, pointer>>>
    cat_ptr& operator=(cat_ptr<U>&& other) noexcept {
        attach(other.detach());
        return *this;
    }

    void swap(cat_ptr& other) noexcept {
        auto ptr = m_ptr;
        m_ptr = other.m_ptr;
        other.m_ptr = ptr;
    }

    void swap(cat_ptr&& other) noexcept {
        swap(other);
    }

    void reset() noexcept {
        auto ptr = m_ptr;
        m_ptr = nullptr;
        if (ptr)
            ptr->release();
    }

    void reset(std::nullptr_t) noexcept {
        reset();
    }

    void attach(pointer other) noexcept {
        auto ptr = m_ptr;
        m_ptr = other;
        if (ptr)
            ptr->release();
    }

    [[nodiscard]] pointer detach() noexcept {
        auto temp = m_ptr;
        m_ptr = nullptr;
        return temp;
    }

    mutable_element_type** put() noexcept {
        reset();
        return const_cast<mutable_element_type**>(&m_ptr);
    }

    std::add_const_t<T>** put_const() noexcept {
        reset();
        return &m_ptr;
    }

    IObject** put_object() noexcept {
        return reinterpret_cast<IObject**>(put());
    }

    pointer* addressof() noexcept {
        return &m_ptr;
    }

    const pointer* addressof() const noexcept {
        return &m_ptr;
    }

    explicit operator bool() const noexcept {
        return m_ptr != nullptr;
    }

    pointer get() const noexcept {
        return m_ptr;
    }

    pointer operator->() const noexcept {
        return m_ptr;
    }

    std::add_lvalue_reference_t<T> operator*() const noexcept {
        return *m_ptr;
    }

    template<typename U> cat_ptr<U> query() const {
        std::add_lvalue_reference_t<T> m = *m_ptr;
        return &dynamic_cast<std::add_lvalue_reference_t<U>>(m);
    }

    template<typename U> cat_ptr<U> try_query() const noexcept {
        return dynamic_cast<U*>(m_ptr);
    }

    cat_ptr<mutable_element_type> try_usurp() noexcept {
        if (m_ptr->is_unique())
            return const_cast<mutable_element_type*>(m_ptr);
        else
            return nullptr;
    }

    cat_ptr<mutable_element_type> clone() const noexcept {
        IObject* out;
        m_ptr->clone(&out);
        return {dynamic_cast<mutable_element_type*>(out), false};
    }

    cat_ptr<mutable_element_type> usurp_or_clone() noexcept {
        if (auto p = try_usurp(); p)
            return p;
        else
            return clone();
    }
};

template<typename T, typename U, typename... Args>
void create_instance(U** out, Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...))) {
    *out = new T(std::forward<Args>(args)...);
    (*out)->add_ref();
}

template<typename T> cat_ptr<T> wrap_cat_ptr(T* p) noexcept {
    return p;
}

inline FrameFormat make_frame_format(ColorFamily color_family, SampleType sample_type, unsigned bits_per_sample,
                                     unsigned width_subsampling, unsigned height_subsampling) noexcept {
    FrameFormat ff;
    ff.detail.color_family = color_family;
    ff.detail.sample_type = sample_type;
    ff.detail.bits_per_sample = bits_per_sample;
    ff.detail.width_subsampling = width_subsampling;
    ff.detail.height_subsampling = height_subsampling;
    return ff;
}

inline FrameFormat get_frame_format_by_id(uint32_t id) noexcept {
    FrameFormat ff;
    ff.id = id;
    return ff;
}

inline unsigned bytes_per_sample(FrameFormat ff) noexcept {
    return (ff.detail.bits_per_sample + 7u) / 8u;
}

inline unsigned num_planes(FrameFormat ff) noexcept {
    return ff.detail.color_family == ColorFamily::Gray ? 1 : 3;
}

inline unsigned plane_width(FrameInfo fi, unsigned idx) noexcept {
    return fi.width >> (idx ? fi.format.detail.width_subsampling : 0u);
}

inline unsigned plane_height(FrameInfo fi, unsigned idx) noexcept {
    return fi.height >> (idx ? fi.format.detail.height_subsampling : 0u);
}

inline size_t width_bytes(FrameInfo fi, unsigned idx) noexcept {
    return static_cast<size_t>(plane_width(fi, idx)) * bytes_per_sample(fi.format);
}

inline size_t default_stride(FrameInfo fi, unsigned idx) noexcept {
    auto align = std::hardware_destructive_interference_size;
    auto stride = (width_bytes(fi, idx) + align - 1u) / align * align;
    return stride;
}

namespace detail {

template<typename F>
class CallbackWrapper final : virtual public ICallback {
    F f;

  public:
    explicit CallbackWrapper(F&& f) noexcept : f(std::move(f)) {}

    void invoke(const IFrame* frame, std::exception_ptr exc) noexcept final {
        std::invoke(f, frame, exc);
    }

    void drop() noexcept final {
        delete this;
    }
};

}

template<typename F>
cat_ptr<ICallback> wrap_callback(F&& f) noexcept {
    return wrap_cat_ptr(new detail::CallbackWrapper{std::move(f)});
}

} // namespace catsyn
