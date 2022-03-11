#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catimpl.h>

class Table final : public Object, public ITable {
    typedef std::vector<std::pair<std::optional<std::string>, cat_ptr<const IObject>>> vector_type;
    vector_type vec;

    size_t norm_ref(size_t ref) const noexcept {
        if (ref == npos)
            return vec.size();
        else
            return ref;
    }

    void expand(size_t len) noexcept {
        if (len > vec.size())
            vec.resize(len);
    }

  public:
    explicit Table(vector_type vec) : vec(std::move(vec)) {}
    explicit Table(size_t reserve_capacity) noexcept {
        vec.reserve(reserve_capacity);
    }

    size_t size() const noexcept final {
        return vec.size();
    }

    const IObject* get(size_t ref) const noexcept final {
        ref = norm_ref(ref);
        if (ref >= size())
            return nullptr;
        else
            return vec[ref].second.get();
    }

    void set(size_t ref, const IObject* obj) noexcept final {
        ref = norm_ref(ref);
        expand(ref + 1);
        vec[ref].second = obj;
    }

    size_t get_ref(const char* key) const noexcept final {
        std::string_view keysv{key};
        for (size_t i = 0; i < size(); ++i)
            if (const auto& item_key = vec[i].first; item_key && item_key.value() == keysv)
                return i;
        return npos;
    }

    const char* get_key(size_t ref) const noexcept final {
        ref = norm_ref(ref);
        if (ref >= size())
            return nullptr;
        if (const auto& item_key = vec[ref].first; item_key)
            return item_key.value().c_str();
        else
            return nullptr;
    }

    void set_key(size_t ref, const char* key) noexcept final {
        ref = norm_ref(ref);
        expand(ref + 1);
        vec[ref].first = key ? std::make_optional(key) : std::nullopt;
    }

    void clone(IObject** out) const noexcept final {
        create_instance<Table>(out, vec);
    }
};

void Nucleus::create_table(size_t reserve_capacity, ITable** out) noexcept {
    create_instance<Table>(out, reserve_capacity);
}
