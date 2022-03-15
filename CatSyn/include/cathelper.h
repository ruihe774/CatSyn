#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

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

    pointer* put_const() noexcept {
        reset();
        return &m_ptr;
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

template<typename T, bool immutable> class TableView {};

template<typename T> class TableView<T, true> {
  public:
    typedef std::enable_if_t<std::is_base_of_v<ITable, std::remove_const_t<T>>, T> table_type;

    cat_ptr<table_type> table;

    explicit TableView(table_type* table) noexcept : table(table) {}
    explicit TableView(cat_ptr<table_type> table) noexcept : table(std::move(table)) {}

  public:
    template<typename U> cat_ptr<const U> get(size_t ref) const {
        if (cat_ptr<const IObject> p = table->get(ref); p)
            return p.query<const U>();
        else
            return nullptr;
    }

    template<typename U> cat_ptr<const U> get(const char* key) const {
        return get<U>(table->get_ref(key));
    }

    template<typename U> catsyn::BytesView<const U> get_array(const char* key) const {
        return catsyn::BytesView<const U>{get<IBytes>(key)};
    }

    size_t size() const noexcept {
        return table->size();
    }
};

template<typename T> class TableView<T, false> : public TableView<T, true> {
    typedef TableView<T, true> Base;

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

    template<typename U> void set(const char* key, const cat_ptr<U>& in) noexcept {
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

    template<typename U> cat_ptr<U> modify(const char* key) {
        auto ref = this->table->get_ref(key);
        if (ref == ITable::npos)
            return nullptr;
        auto p = this->table->get(ref);
        if (p->is_unique())
            return make_cat_ptr(const_cast<IObject*>(p)).template query<U>();
        else {
            auto new_p = make_cat_ptr(p).template query<const U>().clone();
            this->table->set(ref, new_p.get());
            return new_p;
        }
    }
};

} // namespace detail

template<typename T> using TableView = detail::TableView<T, std::is_const_v<T>>;

inline IEnzyme* get_enzyme_by_id(INucleus* nucl, const char* id) noexcept {
    auto enzymes = nucl->get_enzymes();
    return dynamic_cast<IEnzyme*>(const_cast<IObject*>(enzymes->get(enzymes->get_ref(id))));
}

inline IEnzyme* get_enzyme_by_ns(INucleus* nucl, const char* ns) noexcept {
    auto enzymes = nucl->get_enzymes();
    auto size = enzymes->size();
    for (size_t ref = 0; ref < size; ++ref)
        if (auto enzyme = dynamic_cast<IEnzyme*>(const_cast<IObject*>(enzymes->get(ref)));
            enzyme && strcmp(enzyme->get_namespace(), ns) == 0)
            return enzyme;
    return nullptr;
}

inline void check_args(IFunction* func, ITable* args);

namespace detail {
template<typename F> class FunctionWrapper final : public IFunction {
    F f;
    std::vector<ArgSpec> arg_specs;
    const std::type_info* out_type;

  public:
    FunctionWrapper(std::initializer_list<ArgSpec> arg_specs, const std::type_info* out_type, F f) noexcept
        : f(std::move(f)), arg_specs(arg_specs), out_type(out_type) {}

    void invoke(ITable* args, const IObject** out) final {
        check_args(this, args);
        std::invoke(f, args, out);
    }

    void drop() noexcept final {
        delete this;
    }

    const ArgSpec* get_arg_specs(size_t* len) const noexcept final {
        *len = arg_specs.size();
        return arg_specs.data();
    };

    const std::type_info* get_out_type() const noexcept final {
        return out_type;
    }
};
} // namespace detail

template<typename F>
cat_ptr<IFunction> wrap_func(std::initializer_list<ArgSpec> arg_specs, const std::type_info* out_type, F f) noexcept {
    return new detail::FunctionWrapper(arg_specs, out_type, std::move(f));
}

inline void set_table(ITable* table, const char* key, const IObject* val) noexcept {
    auto ref = table->get_ref(key);
    if (ref == ITable::npos) {
        ref = table->size();
        table->set_key(ref, key);
    }
    table->set(ref, val);
}

template<typename U>
inline void set_table(const cat_ptr<ITable>& table, const char* key, const cat_ptr<U>& val) noexcept {
    set_table(table.get(), key, val.get());
}

inline void set_table(const cat_ptr<ITable>& table, const char* key, const IObject* val) noexcept {
    set_table(table.get(), key, val);
}

template<typename T, typename V, typename... Args, typename = std::enable_if_t<sizeof...(Args) != 0>>
void set_table(T table, const char* key, V val, Args&&... args) noexcept {
    set_table(table, key, val);
    set_table(table, std::forward<Args>(args)...);
}

inline cat_ptr<ITable> create_arg_table(IFactory* factory, const ArgSpec* specs, size_t arg_count) noexcept {
    cat_ptr<ITable> table;
    factory->create_table(arg_count, table.put());
    for (size_t ref = 0; ref < arg_count; ++ref)
        table->set_key(ref, specs[ref].name);
    return table;
}


#ifdef _WIN32
#ifdef __clang__
extern "C" void* __RTDynamicCast(void* inptr, long VfDelta, void* SrcType, void* TargetType, int isReference);
#endif
inline void* runtime_dynamic_cast(const std::type_info& dst_type, IObject* src) {
    // XXX: I don't know this is correct or not
    return __RTDynamicCast(src, 0, (void*)&typeid(IObject), (void*)&dst_type, 0);
}
#endif

inline void check_args(const ArgSpec* specs, size_t arg_count, ITable* args) {
    thread_local char buf[4096];
    if (arg_count != args->size()) {
        sprintf(buf, "invalid number of arguments (expected %llu, got %llu)", arg_count, args->size());
        throw std::invalid_argument(buf);
    }
    for (size_t ref = 0; ref < arg_count; ++ref) {
        if (strcmp(specs[ref].name, args->get_key(ref)) != 0) {
            snprintf(buf, sizeof(buf), "invalid argument name at %llu (expected '%s', got '%s')", ref, specs[ref].name,
                     args->get_key(ref));
            throw std::invalid_argument(buf);
        }
        if (specs[ref].required && !args->get(ref)) {
            snprintf(buf, sizeof(buf), "missing required argument '%s'", specs[ref].name);
            throw std::invalid_argument(buf);
        }
        if (auto obj = const_cast<IObject*>(args->get(ref)); obj && !runtime_dynamic_cast(specs[ref].type, obj)) {
            snprintf(buf, sizeof(buf), "invalid type for argument '%s' (expected '%s' or derived, got '%s')",
                     specs[ref].name, specs[ref].type.name(), typeid(obj).name());
            throw std::invalid_argument(buf);
        }
    }
}

inline cat_ptr<ITable> create_arg_table(IFactory* factory, IFunction* func) noexcept {
    size_t len;
    auto specs = func->get_arg_specs(&len);
    return create_arg_table(factory, specs, len);
}

inline void check_args(IFunction* func, ITable* args) {
    size_t len;
    auto specs = func->get_arg_specs(&len);
    check_args(specs, len, args);
}

template<typename R, typename = std::enable_if_t<!std::is_same_v<R, void>>>
cat_ptr<const R> call_func(IFunction* func, ITable* args) {
    cat_ptr<const IObject> out;
    func->invoke(args, out.put_const());
    return out.template query<const R>();
}

template<typename R, typename = std::enable_if_t<!std::is_same_v<R, void>>>
cat_ptr<const R> call_func(const cat_ptr<IFunction>& func, const cat_ptr<ITable>& args) {
    return call_func<R>(func.get(), args.get());
}

template<typename R, typename... Args, typename = std::enable_if_t<!std::is_same_v<R, void>>>
cat_ptr<const R> vcall_func(IFactory* factory, IFunction* func, Args&&... args) {
    auto arg_table = create_arg_table(factory, func);
    if constexpr (sizeof...(args) != 0)
        set_table(arg_table, std::forward<Args>(args)...);
    return call_func<R>(func, arg_table.get());
}

template<typename R, typename... Args, typename = std::enable_if_t<!std::is_same_v<R, void>>>
cat_ptr<const R> vcall_func(IFactory* factory, const cat_ptr<IFunction>& func, Args&&... args) {
    return vcall_func<R>(factory, func.get(), std::forward<Args>(args)...);
}

template<typename R, typename = std::enable_if_t<std::is_same_v<R, void>>>
void call_func(IFunction* func, ITable* args) {
    cat_ptr<const IObject> out;
    func->invoke(args, out.put_const());
    if (out)
        throw std::logic_error("function has return value");
}

template<typename R, typename = std::enable_if_t<std::is_same_v<R, void>>>
void call_func(IFunction* func, const cat_ptr<ITable>& args) {
    call_func<R>(func, args.get());
}

template<typename R, typename... Args, typename = std::enable_if_t<std::is_same_v<R, void>>>
void vcall_func(IFactory* factory, IFunction* func, Args&&... args) {
    auto arg_table = create_arg_table(factory, func);
    set_table(arg_table, std::forward<Args>(args)...);
    call_func<R>(func, arg_table.get());
}

template<typename R, typename... Args, typename = std::enable_if_t<std::is_same_v<R, void>>>
void vcall_func(IFactory* factory, const cat_ptr<IFunction>& func, Args&&... args) {
    vcall_func<R>(factory, func.get(), std::forward<Args>(args)...);
}

template<typename T> ArgSpec required_arg(const char* name) noexcept {
    return ArgSpec{name, typeid(T), true};
}

template<typename T> ArgSpec optional_arg(const char* name) noexcept {
    return ArgSpec{name, typeid(T), false};
}

} // namespace catsyn
