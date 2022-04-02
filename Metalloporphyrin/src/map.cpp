#include <porphyrin.h>

constexpr auto npos = catsyn::ITable::npos;

VSMap::VSMap(const catsyn::ITable* table) noexcept : table(table) {}
VSMap::VSMap(catsyn::cat_ptr<const catsyn::ITable> table) noexcept : table(std::move(table)) {}

catsyn::ITable* VSMap::get_mut() noexcept {
    return const_cast<catsyn::ITable*>(table.get());
}

VSMap* createMap() noexcept {
    catsyn::cat_ptr<catsyn::ITable> table;
    core->nucl->get_factory()->create_table(0, table.put());
    return new VSMap{std::move(table)};
}

void freeMap(VSMap* map) noexcept {
    delete map;
}

void clearMap(VSMap* map) noexcept {
    map->get_mut()->clear();
}

void setError(VSMap* map, const char* errorMessage) noexcept {
    catsyn::cat_ptr<catsyn::IBytes> bytes;
    core->nucl->get_factory()->create_bytes(errorMessage, strlen(errorMessage) + 1, bytes.put());
    map->get_mut()->set(npos, bytes.get(), "__error");
}

const char* getError(const VSMap* map) noexcept {
    if (auto msg = dynamic_cast<const catsyn::IBytes*>(map->table->get(map->table->find("__error"), nullptr)); msg) {
        return static_cast<const char*>(msg->data());
    } else
        return nullptr;
}

int propNumKeys(const VSMap* map) noexcept {
    return static_cast<int>(map->table->size());
}

const char* propGetKey(const VSMap* map, int index) noexcept {
    for (size_t ref = map->table->next(npos), i = 0; ref != npos; ref = map->table->next(ref), ++i)
        if (i == index) {
            const char* key;
            map->table->get(ref, &key);
            return key;
        }
    return nullptr;
}

int propDeleteKey(VSMap* map, const char* key) noexcept {
    auto table = map->get_mut();
    auto ref = table->find(key);
    if (ref != npos) {
        table->set(ref, nullptr, nullptr);
        return 1;
    } else
        return 0;
}

char propGetType(const VSMap* map, const char* key) noexcept {
    auto val = map->table->get(map->table->find(key), nullptr);
    if (!val)
        return ptUnset;
    if (auto arr = dynamic_cast<const catsyn::INumeric*>(val); arr) {
        if (arr->sample_type == catsyn::SampleType::Integer)
            return ptInt;
        else
            return ptFloat;
    }
    if (auto p = dynamic_cast<const catsyn::ITable*>(val); p)
        val = p->get(0, nullptr);
    if (dynamic_cast<const catsyn::IBytes*>(val))
        return ptData;
    else if (dynamic_cast<const catsyn::ISubstrate*>(val))
        return ptNode;
    else if (dynamic_cast<const catsyn::IFrame*>(val))
        return ptFrame;
    else if (dynamic_cast<const catsyn::IFunction*>(val))
        return ptFunction;
    else
        return ptUnset;
}

int propNumElements(const VSMap* map, const char* key) noexcept {
    auto val = map->table->get(map->table->find(key), nullptr);
    if (!val)
        return -1;
    if (auto arr = dynamic_cast<const catsyn::INumeric*>(val); arr)
        return static_cast<int>(arr->bytes_count() / 8);
    if (auto arr = dynamic_cast<const catsyn::ITable*>(val); arr)
        return static_cast<int>(arr->size());
    return 1;
}

template<catsyn::SampleType sample_type> auto map_get_array(const VSMap* map, const char* key, int* error) noexcept {
    using rt = std::conditional_t<sample_type == catsyn::SampleType::Integer, int64_t, double>;
    std::pair<const rt*, size_t> fr{nullptr, 0};
    if (error)
        *error = 0;
    auto val = map->table->get(map->table->find(key), nullptr);
    if (!val) {
        *error = peUnset;
        return fr;
    }
    if (auto arr = dynamic_cast<const catsyn::INumeric*>(val); arr && arr->sample_type == sample_type)
        return std::pair<const rt*, size_t>{static_cast<const rt*>(arr->data()), arr->bytes_count() / 8};
    *error = peType;
    return fr;
}

const int64_t* propGetIntArray(const VSMap* map, const char* key, int* error) noexcept {
    return map_get_array<catsyn::SampleType::Integer>(map, key, error).first;
}

const double* propGetFloatArray(const VSMap* map, const char* key, int* error) noexcept {
    return map_get_array<catsyn::SampleType::Float>(map, key, error).first;
}

template<catsyn::SampleType sample_type>
auto map_get_numeric(const VSMap* map, const char* key, int index, int* error) noexcept {
    using rt = std::conditional_t<sample_type == catsyn::SampleType::Integer, int64_t, double>;
    auto [arr, size] = map_get_array<sample_type>(map, key, error);
    if (!arr)
        return static_cast<rt>(0);
    if (index >= size) {
        *error = peIndex;
        return static_cast<rt>(0);
    }
    return arr[index];
}

int64_t propGetInt(const VSMap* map, const char* key, int index, int* error) noexcept {
    return map_get_numeric<catsyn::SampleType::Integer>(map, key, index, error);
}

double propGetFloat(const VSMap* map, const char* key, int index, int* error) noexcept {
    return map_get_numeric<catsyn::SampleType::Float>(map, key, index, error);
}

template<typename T> const T* map_get(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    auto val = map->table->get(map->table->find(key), nullptr);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    if (auto arr = dynamic_cast<const catsyn::ITable*>(val); arr) {
        val = arr->get(index, nullptr);
        if (!val) {
            *error = peIndex;
            return nullptr;
        }
    } else if (index) {
        *error = peIndex;
        return nullptr;
    }
    if (auto p = dynamic_cast<const T*>(val); p)
        return p;
    *error = peType;
    return nullptr;
}

const char* propGetData(const VSMap* map, const char* key, int index, int* error) noexcept {
    auto data = map_get<catsyn::IBytes>(map, key, index, error);
    return data ? static_cast<const char*>(data->data()) : nullptr;
}

int propGetDataSize(const VSMap* map, const char* key, int index, int* error) noexcept {
    auto data = map_get<catsyn::IBytes>(map, key, index, error);
    return data ? static_cast<int>(data->size()) - 1 : 0;
}

template<catsyn::SampleType sample_type>
int map_set_array(VSMap* map, const char* key,
                  std::conditional_t<sample_type == catsyn::SampleType::Integer, const int64_t*, const double*> i,
                  int size) noexcept {
    if (size < 0)
        return 1;
    catsyn::cat_ptr<catsyn::INumeric> arr;
    core->nucl->get_factory()->create_numeric(sample_type, i, size * 8, arr.put());
    map->get_mut()->set(map->table->find(key), arr.get(), key);
    return 0;
}

template<catsyn::SampleType sample_type>
int map_set_numeric(VSMap* map, const char* key,
                    std::conditional_t<sample_type == catsyn::SampleType::Integer, int64_t, double> value,
                    int append) noexcept {
    if (append == paTouch)
        return 0;
    auto ref = map->table->find(key);
    if (append == paAppend && ref != npos) {
        auto val = map->table->get(ref, nullptr);
        if (auto arr = dynamic_cast<const catsyn::INumeric*>(val); arr) {
            if (arr->sample_type != sample_type)
                return 1;
            catsyn::cat_ptr<catsyn::INumeric> marr;
            if (arr->is_unique())
                marr = catsyn::wrap_cat_ptr(const_cast<catsyn::INumeric*>(arr));
            else {
                arr->clone(marr.put_object());
                map->get_mut()->set(ref, marr.get(), nullptr);
            }
            auto index = marr->bytes_count() / 8;
            marr->realloc(index * 8 + 8);
            static_cast<decltype(value)*>(marr->data())[index] = value;
            return 0;
        }
        return 1;
    }
    catsyn::cat_ptr<catsyn::INumeric> arr;
    core->nucl->get_factory()->create_numeric(sample_type, &value, sizeof(value), arr.put());
    map->get_mut()->set(ref, arr.get(), key);
    return 0;
}

int propSetInt(VSMap* map, const char* key, int64_t i, int append) noexcept {
    return map_set_numeric<catsyn::SampleType::Integer>(map, key, i, append);
}

int propSetIntArray(VSMap* map, const char* key, const int64_t* i, int size) noexcept {
    return map_set_array<catsyn::SampleType::Integer>(map, key, i, size);
}

int propSetFloat(VSMap* map, const char* key, double i, int append) noexcept {
    return map_set_numeric<catsyn::SampleType::Float>(map, key, i, append);
}

int propSetFloatArray(VSMap* map, const char* key, const double* i, int size) noexcept {
    return map_set_array<catsyn::SampleType::Float>(map, key, i, size);
}

template<typename T> int map_set(VSMap* map, const char* key, const T* value, int append) noexcept {
    if (append == paTouch)
        return 0;
    auto ref = map->table->find(key);
    if (append == paAppend && ref != npos) {
        auto val = map->table->get(ref, nullptr);
        if (auto arr = dynamic_cast<const catsyn::ITable*>(val); arr) {
            catsyn::cat_ptr<catsyn::ITable> marr;
            if (arr->is_unique())
                marr = catsyn::wrap_cat_ptr(const_cast<catsyn::ITable*>(arr));
            else {
                arr->clone(marr.put_object());
                map->get_mut()->set(ref, marr.get(), nullptr);
            }
            marr->set(npos, value, nullptr);
            return 0;
        } else if (val) {
            catsyn::cat_ptr<catsyn::ITable> marr;
            core->nucl->get_factory()->create_table(2, marr.put());
            marr->set(0, val, nullptr);
            marr->set(1, value, nullptr);
            map->get_mut()->set(ref, marr.get(), nullptr);
            return 0;
        }
    }
    map->get_mut()->set(ref, value, key);
    return 0;
}

int propSetData(VSMap* map, const char* key, const char* data, int size, int append) noexcept {
    catsyn::cat_ptr<catsyn::IBytes> bytes;
    if (size < 0)
        size = static_cast<int>(strlen(data));
    core->nucl->get_factory()->create_bytes(nullptr, size + 1, bytes.put());
    auto ptr = bytes->data();
    memcpy(ptr, data, size);
    static_cast<char*>(ptr)[size] = 0;
    return map_set(map, key, bytes.get(), append);
}

static VSVideoInfo vi_cs_to_vs(catsyn::VideoInfo vi) {
    return VSVideoInfo{registerFormat(vi.frame_info.format),
                       vi.fps.num,
                       vi.fps.den,
                       static_cast<int>(vi.frame_info.width),
                       static_cast<int>(vi.frame_info.height),
                       static_cast<int>(vi.frame_count),
                       0};
}

VSNodeRef* propGetNode(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    int err;
    auto substrate = const_cast<catsyn::ISubstrate*>(map_get<catsyn::ISubstrate>(map, key, index, &err));
    if (substrate)
    success:
        return new VSNodeRef{substrate, nullptr, vi_cs_to_vs(substrate->get_video_info())};
    else if (err == peType) {
        auto filter = map_get<catsyn::IFilter>(map, key, index, &err);
        if (filter) {
            substrate = core->nucl->register_filter(filter);
            goto success;
        }
    }
    *error = err;
    return nullptr;
}

const VSFrameRef* propGetFrame(const VSMap* map, const char* key, int index, int* error) noexcept {
    auto frame = map_get<catsyn::IFrame>(map, key, index, error);
    if (frame)
        return new VSFrameRef{frame};
    else
        return nullptr;
}

VSFuncRef* propGetFunc(const VSMap* map, const char* key, int index, int* error) noexcept {
    auto func = const_cast<catsyn::IFunction*>(map_get<catsyn::IFunction>(map, key, index, error));
    if (func)
        return new VSFuncRef{func};
    else
        return nullptr;
}

int propSetNode(VSMap* map, const char* key, VSNodeRef* node, int append) noexcept {
    return map_set(map, key, node->substrate.get(), append);
}

int propSetFrame(VSMap* map, const char* key, const VSFrameRef* f, int append) noexcept {
    return map_set(map, key, f->frame.get(), append);
}

int propSetFunc(VSMap* map, const char* key, VSFuncRef* func, int append) noexcept {
    return map_set(map, key, func->func.get(), append);
}
