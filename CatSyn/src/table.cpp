#include <string_view>

#include <catimpl.h>

const IObject* Table::get(size_t ref, const char** key_out) const noexcept {
    if (ref >= vec.size())
        return nullptr;
    auto&& item = vec[ref];
    if (key_out) {
        if (item.first)
            *key_out = item.first.value().c_str();
        else
            *key_out = nullptr;
    }
    return item.second.get();
}

void Table::set(size_t ref, const IObject* obj, const char* key) noexcept {
    if (ref == npos)
        ref = vec.size();
    if (ref >= vec.size())
        vec.resize(ref + 1);
    auto&& item = vec[ref];
    if (key && (!item.first || item.first.value() != key))
        item.first = key;
    item.second = obj;
}

size_t Table::erase(size_t ref) noexcept {
    if (ref >= vec.size())
        return npos;
    vec[ref] = {};
    return next(ref);
}

size_t Table::find(const char* key) const noexcept {
    size_t ref = 0;
    for (auto&& item : vec) {
        if (item.first && item.first.value() == key)
            return ref;
        ++ref;
    }
    return npos;
}

size_t Table::size() const noexcept {
    size_t size = 0;
    for (auto ref = next(npos); ref != npos; ++size)
        ref = next(ref);
    return size;
}

void Table::clear() noexcept {
    vec.clear();
}

size_t Table::next(size_t ref) const noexcept {
    for (auto i = ref + 1; i < vec.size(); ++i)
        if (vec[i].second)
            return i;
    return npos;
}

size_t Table::prev(size_t ref) const noexcept {
    for (auto i = static_cast<std::make_signed_t<size_t>>(ref) - 1; i >= 0; --i)
        if (vec[i].second)
            return i;
    return npos;
}

Table::Table(const Table& other) noexcept {
    const char* key;
    vec.reserve(other.vec.size());
    for (auto ref = other.next(npos); ref != npos; ref = other.next(ref))
        vec.emplace_back(key, other.get(ref, &key));
}

Table::Table(size_t reserve_capacity) noexcept {
    vec.reserve(reserve_capacity);
}

void Table::clone(IObject** out) const noexcept {
    create_instance<Table>(out, *this);
}

void Nucleus::create_table(size_t reserve_capacity, ITable** out) noexcept {
    create_instance<Table>(out, reserve_capacity);
}
