#pragma once

#include <cstddef>
#include <memory>
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

    cat_ptr<mutable_element_type> try_usurp() const noexcept {
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

    cat_ptr<mutable_element_type> usurp_or_clone() const noexcept {
        if (auto p = try_usurp(); p)
            return p;
        else
            return clone();
    }
};

template<typename T> cat_ptr<T> make_cat_ptr(T* p) noexcept {
    return p;
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

inline uintptr_t width_bytes(FrameInfo fi, unsigned idx) noexcept {
    return static_cast<uintptr_t>(plane_width(fi, idx)) * bytes_per_sample(fi.format);
}

inline uintptr_t default_stride(FrameInfo fi, unsigned idx) noexcept {
    auto align = static_cast<uintptr_t>(IAlignedBytes::alignment);
    auto stride = (width_bytes(fi, idx) + align - 1u) / align * align;
    return stride;
}

cat_ptr<IFrame> mask_clone_frame(IFactory* factory, IFrame* src, unsigned int copy_mask) noexcept {
    auto fi = src->get_frame_info();
    auto count = num_planes(fi.format);
    const IAlignedBytes* planes[32];
    uintptr_t strides[32];
    for (unsigned idx = 0; idx < count; ++idx, copy_mask >>= 1) {
        planes[idx] = copy_mask & 1 ? src->get_plane(idx) : nullptr;
        strides[idx] = copy_mask & 1 ? src->get_stride(idx) : 0;
    }
    IFrame* out;
    factory->create_frame(fi, planes, strides, src->get_frame_props(), &out);
    return {out, false};
}

} // namespace catsyn
