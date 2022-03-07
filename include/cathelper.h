#pragma once

#include <type_traits>

#include <catsyn.h>

namespace catsyn {
template<typename T> class cat_ptr {
    T* m_ptr;

  public:
    typedef T element_type;
    typedef T* pointer;

    cat_ptr() noexcept : m_ptr(nullptr) {}

    cat_ptr(std::nullptr_t) noexcept : cat_ptr() {}

    cat_ptr(pointer ptr) noexcept : m_ptr(ptr) {
        if (m_ptr)
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

    pointer* put() noexcept {
        reset();
        return &m_ptr;
    }

    pointer* operator&() noexcept {
        return put();
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
};
} // namespace catsyn
