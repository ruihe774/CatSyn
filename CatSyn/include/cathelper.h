#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
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

template<typename T, typename U, typename... Args>
void create_instance(U** out, Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...))) {
    *out = new T(std::forward<Args>(args)...);
    (*out)->add_ref();
}

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

inline size_t width_bytes(FrameInfo fi, unsigned idx) noexcept {
    return static_cast<size_t>(plane_width(fi, idx)) * bytes_per_sample(fi.format);
}

inline size_t default_stride(FrameInfo fi, unsigned idx) noexcept {
    auto align = static_cast<size_t>(IAlignedBytes::alignment);
    auto stride = (width_bytes(fi, idx) + align - 1u) / align * align;
    return stride;
}

inline cat_ptr<IFrame> mask_clone_frame(IFactory* factory, IFrame* src, unsigned int copy_mask) noexcept {
    auto fi = src->get_frame_info();
    auto count = num_planes(fi.format);
    const IAlignedBytes* planes[32];
    size_t strides[32];
    for (unsigned idx = 0; idx < count; ++idx, copy_mask >>= 1) {
        planes[idx] = copy_mask & 1 ? src->get_plane(idx) : nullptr;
        strides[idx] = copy_mask & 1 ? src->get_stride(idx) : 0;
    }
    IFrame* out;
    factory->create_frame(fi, planes, strides, src->get_frame_props(), &out);
    return {out, false};
}

namespace detail {

template<typename T, bool c> struct add_const_if {};
template<typename T> struct add_const_if<T, true> { typedef std::add_const_t<T> type; };
template<typename T> struct add_const_if<T, false> { typedef T type; };
template<typename T, bool c> using add_const_if_t = typename add_const_if<T, c>::type;
template<typename To, typename From> using transfer_const_t = add_const_if_t<To, std::is_const_v<From>>;

template<typename T, bool immutable> class BytesView {};

template<typename T> class BytesView<T, true> {
  public:
    typedef transfer_const_t<IBytes, T> bytes_type;
    typedef T element_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T* iterator;
    typedef const T* const_iterator;
    typedef T& reference;
    typedef const T& const_reference;

    static constexpr size_t bytes_per_sample = sizeof(T);

    cat_ptr<bytes_type> bytes;

  private:
    void check_size() const {
        if (bytes->size() % bytes_per_sample)
            throw std::invalid_argument("invalid bytes size");
    }

  public:
    explicit BytesView(bytes_type* bytes) noexcept : bytes(bytes) {
        check_size();
    }

    explicit BytesView(cat_ptr<bytes_type> bytes) noexcept : bytes(std::move(bytes)) {
        check_size();
    }

    const_pointer data() const noexcept {
        return static_cast<const T*>(bytes->data());
    }

    size_t size() const noexcept {
        return bytes->size() / bytes_per_sample;
    }

    const_iterator begin() const noexcept {
        return data();
    }

    const_iterator end() const noexcept {
        return begin() + size();
    }

    const_reference operator[](size_t idx) const noexcept {
        return data()[idx];
    }
};

template<typename T> class BytesView<T, false> : public BytesView<T, true> {
    typedef BytesView<T, true> Base;

  public:
    using Base::bytes_per_sample;
    using Base::BytesView;
    using typename Base::bytes_type;
    using typename Base::const_iterator;
    using typename Base::const_pointer;
    using typename Base::const_reference;
    using typename Base::element_type;
    using typename Base::iterator;
    using typename Base::pointer;
    using typename Base::reference;

    pointer data() noexcept {
        return static_cast<pointer>(this->bytes->data());
    }

    iterator begin() noexcept {
        return data();
    }

    iterator end() noexcept {
        return begin() + this->size();
    }

    reference operator[](size_t idx) noexcept {
        return data()[idx];
    }
};

} // namespace detail

template<typename T> using BytesView = detail::BytesView<T, std::is_const_v<T>>;

namespace detail {

template<typename T, typename U, bool immutable> class TableView {};

template<typename T, typename U> class TableViewIterator;

template<typename T, typename U>
bool operator<(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept;

template<typename T, typename U>
size_t operator-(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept;

template<typename T, typename U> class TableViewIterator {
    const TableView<T, U, true>& view;
    size_t ref;

    friend class TableView<T, U, true>;
    friend class TableView<T, U, false>;
    friend bool operator< <T, U>(const TableViewIterator&, const TableViewIterator&) noexcept;
    friend size_t operator- <T, U>(const TableViewIterator&, const TableViewIterator&) noexcept;

    TableViewIterator(const TableView<T, U, true>& view, size_t ref) noexcept : view(view), ref(ref) {}

  public:
    TableViewIterator(const TableViewIterator&) noexcept = default;
    TableViewIterator(TableViewIterator&&) noexcept = default;

    auto operator*() const noexcept {
        return view.template get<U>(ref);
    }

    auto& operator++() noexcept {
        ++ref;
        return *this;
    }

    auto operator++(int) noexcept {
        auto old = *this;
        operator++();
        return old;
    }

    auto& operator--() noexcept {
        --ref;
        return *this;
    }

    auto operator--(int) noexcept {
        auto old = *this;
        operator--();
        return old;
    }

    auto& operator+=(size_t inc) noexcept {
        ref += inc;
        return *this;
    }

    auto& operator-=(size_t dec) noexcept {
        ref -= dec;
        return *this;
    }
};

template<typename T, typename U>
bool operator<(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    if (&lhs.view != &rhs.view)
        return &lhs.view < &rhs.view;
    else
        return lhs.ref < rhs.ref;
}

template<typename T, typename U>
bool operator>(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    return rhs < lhs;
}

template<typename T, typename U>
bool operator==(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    return !(lhs < rhs) && !(lhs > rhs);
}

template<typename T, typename U>
bool operator!=(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    return !(lhs == rhs);
}

template<typename T, typename U>
bool operator<=(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    return lhs < rhs || lhs == rhs;
}

template<typename T, typename U>
bool operator>=(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    return lhs > rhs || lhs == rhs;
}

template<typename T, typename U> auto operator+(TableViewIterator<T, U> lhs, size_t rhs) noexcept {
    return lhs += rhs;
}

template<typename T, typename U> auto operator-(TableViewIterator<T, U> lhs, size_t rhs) noexcept {
    return lhs -= rhs;
}

template<typename T, typename U>
size_t operator-(const TableViewIterator<T, U>& lhs, const TableViewIterator<T, U>& rhs) noexcept {
    return lhs.ref - rhs.ref;
}

template<typename T, typename U> class TableView<T, U, true> {
  public:
    typedef std::enable_if_t<std::is_base_of_v<ITable, std::remove_const_t<T>>, T> table_type;

    cat_ptr<table_type> table;

    friend class TableViewIterator<T, U>;

    explicit TableView(table_type* table) noexcept : table(table) {}
    explicit TableView(cat_ptr<table_type> table) noexcept : table(std::move(table)) {}
    template<typename W, bool immutable>
    explicit TableView(TableView<T, W, immutable> other) : table(std::move(other.table)) {}

  protected:
    template<typename V> cat_ptr<const V> get(size_t ref) const {
        if (cat_ptr<const IObject> p = table->get(ref); p)
            return p.query<const V>();
        else
            return nullptr;
    }

  public:
    template<typename V> cat_ptr<const V> get(const char* key) const {
        return get<V>(table->get_ref(key));
    }

    template<typename V> catsyn::BytesView<const V> get_array(const char* key) const {
        return catsyn::BytesView<const V>{get<IBytes>(key)};
    }

    size_t size() const noexcept {
        return table->size();
    }

    auto begin() const noexcept {
        return TableViewIterator<T, U>(*this, 0);
    }

    auto end() const noexcept {
        return TableViewIterator<T, U>(*this, size());
    }

    cat_ptr<const U> operator[](const char* key) const noexcept {
        return get<U>(key);
    }

    template<typename W> auto as_type() const noexcept {
        return TableView<T, W, true>(*this);
    }
};

template<typename T, typename U> class TableView<T, U, false> : public TableView<T, U, true> {
    typedef TableView<T, U, true> Base;

  public:
    using Base::TableView;
    using typename Base::table_type;

  public:
    void set(const char* key, const IObject* in) noexcept {
        auto ref = this->table->get_ref(key);
        if (ref == ITable::npos) {
            ref = this->size();
            this->table->set_key(ref, key);
        }
        this->table->set(ref, in);
    }

    template<typename V> void set(const char* key, const cat_ptr<V>& in) noexcept {
        set(key, in.get());
    }

    void del(const char* key) noexcept {
        // actually it leases holes
        auto ref = this->table->get_ref(key);
        if (ref == ITable::npos)
            return;
        this->table->set(ref, nullptr);
        this->table->set_key(ref, nullptr);
    }

    template<typename V> cat_ptr<V> modify(const char* key) {
        auto ref = this->table->get_ref(key);
        if (ref == ITable::npos)
            return nullptr;
        auto p = this->table->get(ref);
        if (p->is_unique())
            return make_cat_ptr(const_cast<IObject*>(p)).template query<V>();
        else {
            auto new_p = make_cat_ptr(p).template query<const V>().clone();
            this->table->set(ref, new_p.get());
            return new_p;
        }
    }
};

} // namespace detail

template<typename T> using TableView = detail::TableView<T, IObject, std::is_const_v<T>>;
template<typename T, typename U> using TypedTableView = detail::TableView<T, U, std::is_const_v<T>>;

} // namespace catsyn
