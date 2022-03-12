#include <string_view>
#include <vector>

#include <catimpl.h>

size_t Table::norm_ref(size_t ref) const noexcept {
    if (ref == npos)
        return vec.size();
    else
        return ref;
}

void Table::expand(size_t len) noexcept {
    if (len > vec.size())
        vec.resize(len);
}

Table::Table(vector_type vec) noexcept : vec(std::move(vec)) {}
Table::Table(size_t reserve_capacity) noexcept {
    vec.reserve(reserve_capacity);
}

size_t Table::size() const noexcept {
    return vec.size();
}

const IObject* Table::get(size_t ref) const noexcept {
    ref = norm_ref(ref);
    if (ref >= size())
        return nullptr;
    else
        return vec[ref].second.get();
}

void Table::set(size_t ref, const IObject* obj) noexcept {
    ref = norm_ref(ref);
    expand(ref + 1);
    vec[ref].second = obj;
}

size_t Table::get_ref(const char* key) const noexcept {
    std::string_view keysv{key};
    for (size_t i = 0; i < size(); ++i)
        if (const auto& item_key = vec[i].first; item_key && item_key.value() == keysv)
            return i;
    return npos;
}

const char* Table::get_key(size_t ref) const noexcept {
    ref = norm_ref(ref);
    if (ref >= size())
        return nullptr;
    if (const auto& item_key = vec[ref].first; item_key)
        return item_key.value().c_str();
    else
        return nullptr;
}

void Table::set_key(size_t ref, const char* key) noexcept {
    ref = norm_ref(ref);
    expand(ref + 1);
    vec[ref].first = key ? std::make_optional(key) : std::nullopt;
}

void Table::clone(IObject** out) const noexcept {
    create_instance<Table>(out, vec);
}

void Nucleus::create_table(size_t reserve_capacity, ITable** out) noexcept {
    create_instance<Table>(out, reserve_capacity);
}
