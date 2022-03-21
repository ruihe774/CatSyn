#include <fmt/format.h>

#include <porphyrin.h>

VSMap::VSMap(const catsyn::ITable* table) noexcept : view(table) {}
VSMap::VSMap(catsyn::cat_ptr<const catsyn::ITable> table) noexcept : view(std::move(table)) {}

catsyn::TableView<catsyn::ITable>& VSMap::get_mut() noexcept {
    return reinterpret_cast<catsyn::TableView<catsyn::ITable>&>(view);
}

VSCore* createCore(int threads, bool temporary) noexcept;

VSMap* createMap() noexcept {
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    if (cores.empty()) {
        // race!!
        lock.unlock();
        createCore(0, true);
    }
    catsyn::cat_ptr<catsyn::ITable> table;
    cores.front()->nucl->get_factory()->create_table(0, table.put());
    return new VSMap(std::move(table));
}

void freeMap(VSMap* map) noexcept {
    delete map;
}

void clearMap(VSMap* map) noexcept {
    map->get_mut().table->clear();
}

void setError(VSMap* map, const char* errorMessage) noexcept {
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    catsyn::cat_ptr<catsyn::IBytes> bytes;
    cores.front()->nucl->get_factory()->create_bytes(errorMessage, strlen(errorMessage) + 1, bytes.put());
    map->get_mut().set("__error", bytes.get());
}

const char* getError(const VSMap* map) noexcept {
    // leak
    if (auto msg = map->view.get<catsyn::IBytes>("__error"); msg) {
        return strdup(static_cast<const char*>(msg->data()));
    } else
        return nullptr;
}

int propNumKeys(const VSMap* map) noexcept {
    return static_cast<int>(map->view.size());
}

const char* propGetKey(const VSMap* map, int index) noexcept {
    return map->view.table->get_key(index);
}

int propDeleteKey(VSMap* map, const char* key) noexcept {
    int exists = !!map->view.get<catsyn::IObject>(key);
    map->get_mut().del(key);
//    if (exists)
//        logMessage(mtWarning, "Metalloporphyrin: hole left (propDeleteKey)");
    return exists;
}

char propGetType(const VSMap* map, const char* key) noexcept {
    auto val = map->view.get<catsyn::IObject>(key);
    if (auto p = val.try_query<const catsyn::INumberArray>(); p) {
        if (p->sample_type == catsyn::SampleType::Integer)
            return ptInt;
        else
            return ptFloat;
    }
    if (val.try_query<const catsyn::IBytes>())
        return ptData;
    if (val.try_query<const catsyn::ISubstrate>())
        return ptNode;
    if (val.try_query<const catsyn::IFunction>())
        return ptFunction;
    return ptUnset;
}

[[noreturn]] static void insufficient_buffer() {
    throw std::runtime_error("insufficient buffer");
}

template<typename... Args> const char* format_c(fmt::format_string<Args...> fmt, Args&&... args) noexcept {
    thread_local char buf[4096];
    auto size = fmt::format_to_n(buf, sizeof(buf) - 1, std::move(fmt), std::forward<Args>(args)...).size;
    if (size >= sizeof(buf))
        insufficient_buffer();
    buf[size] = 0;
    return buf;
}

int propNumElements(const VSMap* map, const char* key) noexcept {
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val)
        return -1;
    if (auto arr = val.try_query<const catsyn::INumberArray>(); arr)
        return arr->size() / 8;
    else if (val.try_query<const catsyn::ISubstrate>() || val.try_query<const catsyn::IBytes>()) {
        auto table = map->view.table;
        for (unsigned i = 1; ; ++i) {
            auto nk = format_c("__{}{}", key, i);
            auto ref = table->get_ref(nk);
            if (ref == catsyn::ITable::npos) {
                return i;
            }
        }
    } else
        return 1;
}

static const catsyn::INumberArray* propGetIntArrayImpl(const VSMap* map, const char* key, int* error) noexcept {
    if (error)
        *error = 0;
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    auto arr = val.try_query<const catsyn::INumberArray>();
    if (!arr || arr->sample_type != catsyn::SampleType::Integer) {
        *error = peType;
        return nullptr;
    }
    return arr.get();
}

const int64_t* propGetIntArray(const VSMap* map, const char* key, int* error) noexcept {
    auto arr = propGetIntArrayImpl(map, key, error);
    return arr ? static_cast<const int64_t*>(arr->data()) : nullptr;
}

static const catsyn::INumberArray* propGetFloatArrayImpl(const VSMap* map, const char* key, int* error) noexcept {
    if (error)
        *error = 0;
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    auto arr = val.try_query<const catsyn::INumberArray>();
    if (!arr || arr->sample_type != catsyn::SampleType::Float) {
        *error = peType;
        return nullptr;
    }
    return arr.get();
}

const double* propGetFloatArray(const VSMap* map, const char* key, int* error) noexcept {
    auto arr = propGetFloatArrayImpl(map, key, error);
    return arr ? static_cast<const double*>(arr->data()) : nullptr;
}

int64_t propGetInt(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    auto arr = propGetIntArrayImpl(map, key, error);
    if (!arr)
        return 0;
    if (index >= arr->size() / 8) {
        *error = peIndex;
        return 0;
    }
    return static_cast<const int64_t*>(arr->data())[index];
}

double propGetFloat(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    auto arr = propGetFloatArrayImpl(map, key, error);
    if (!arr)
        return 0;
    if (index >= arr->size() / 8) {
        *error = peIndex;
        return 0;
    }
    return static_cast<const double*>(arr->data())[index];
}

static const catsyn::IBytes* propGetDataImpl(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    if (index) {
        key = format_c("__{}{}", key, index);
    }
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    auto data = val.try_query<const catsyn::IBytes>();
    if (!data) {
        *error = peType;
        return nullptr;
    }
    return data.get();
}

const char* propGetData(const VSMap* map, const char* key, int index, int* error) noexcept {
    auto data = propGetDataImpl(map, key, index, error);
    return data ? static_cast<const char*>(data->data()) : nullptr;
}

int propGetDataSize(const VSMap* map, const char* key, int index, int* error) noexcept {
    return static_cast<int>(propGetDataImpl(map, key, index, error)->size() - 1);
}

int propSetInt(VSMap* map, const char* key, int64_t i, int append) noexcept {
    auto table = map->get_mut();
    catsyn::cat_ptr<catsyn::INumberArray> arr;
    try {
        if (append == paTouch && table.get<catsyn::INumberArray>(key))
            return 0;
        arr = table.modify<catsyn::INumberArray>(key);
    } catch (std::bad_cast&) {
        return 1;
    }
    if (!arr) {
        std::shared_lock<std::shared_mutex> lock(cores_mutex);
        cores.front()->nucl->get_factory()->create_number_array(catsyn::SampleType::Integer, nullptr, 0, arr.put());
        table.set(key, arr.get());
    }
    if (arr->sample_type != catsyn::SampleType::Integer)
        return 1;
    if (append == paReplace) {
        arr->realloc(8);
        static_cast<int64_t*>(arr->data())[0] = i;
    } else if (append == paAppend) {
        auto size = arr->size();
        arr->realloc(size + 8);
        static_cast<int64_t*>(arr->data())[size / 8] = i;
    }
    return 0;
}

int propSetIntArray(VSMap* map, const char* key, const int64_t* i, int size) noexcept {
    if (size < 0)
        return 1;
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    catsyn::cat_ptr<catsyn::INumberArray> arr;
    cores.front()->nucl->get_factory()->create_number_array(catsyn::SampleType::Integer, i, size * 8, arr.put());
    map->get_mut().set(key, arr.get());
    return 0;
}

int propSetFloat(VSMap* map, const char* key, double i, int append) noexcept {
    auto table = map->get_mut();
    catsyn::cat_ptr<catsyn::INumberArray> arr;
    try {
        if (append == paTouch && table.get<catsyn::INumberArray>(key))
            return 0;
        arr = table.modify<catsyn::INumberArray>(key);
    } catch (std::bad_cast&) {
        return 1;
    }
    if (!arr) {
        std::shared_lock<std::shared_mutex> lock(cores_mutex);
        cores.front()->nucl->get_factory()->create_number_array(catsyn::SampleType::Float, nullptr, 0, arr.put());
        table.set(key, arr.get());
    }
    if (arr->sample_type != catsyn::SampleType::Float)
        return 1;
    if (append == paReplace) {
        arr->realloc(8);
        static_cast<double*>(arr->data())[0] = i;
    } else if (append == paAppend) {
        auto size = arr->size();
        arr->realloc(size + 8);
        static_cast<double*>(arr->data())[size / 8] = i;
    }
    return 0;
}

int propSetFloatArray(VSMap* map, const char* key, const double* i, int size) noexcept {
    if (size < 0)
        return 1;
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    catsyn::cat_ptr<catsyn::INumberArray> arr;
    cores.front()->nucl->get_factory()->create_number_array(catsyn::SampleType::Float, i, size * 8, arr.put());
    map->get_mut().set(key, arr.get());
    return 0;
}

int propSetData(VSMap* map, const char* key, const char* data, int size, int append) noexcept {
    auto table = map->get_mut();
    try {
        auto bytes = table.get<catsyn::IBytes>(key);
        if (bytes && append == paTouch)
            return 0;
        if (bytes && append == paAppend) {
            for (unsigned i = 1; ; ++i) {
                auto nk = format_c("__{}{}", key, i);
                auto ref = table.table->get_ref(nk);
                if (ref == catsyn::ITable::npos) {
                    key = nk;
                    break;
                }
            }
        }
    } catch (std::bad_cast&) {
        return 1;
    }
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    catsyn::cat_ptr<catsyn::IBytes> bytes;
    cores.front()->nucl->get_factory()->create_bytes(nullptr, append == paTouch ? 1 : size + 1, bytes.put());
    if (append != paTouch)
        memcpy(bytes->data(), data, size);
    static_cast<char*>(bytes->data())[bytes->size() - 1] = 0;
    table.set(key, bytes.get());
    return 0;
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
    if (index) {
        key = format_c("__{}{}", key, index);
    }
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    auto substrate = val.try_query<const catsyn::ISubstrate>().clone();
    if (!substrate) {
        *error = peType;
        return nullptr;
    }
    auto& nucl = *substrate->get_nucleus();
    auto vi = vi_cs_to_vs(substrate->get_video_info());
    return new VSNodeRef{nucl, std::move(substrate), nullptr, vi};
}

const VSFrameRef* propGetFrame(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    if (index) {
        *error = peIndex;
        return nullptr;
    }
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    auto frame = val.try_query<const catsyn::IFrame>();
    if (!frame) {
        *error = peType;
        return nullptr;
    }
    return new VSFrameRef{frame};
}

VSFuncRef* propGetFunc(const VSMap* map, const char* key, int index, int* error) noexcept {
    if (error)
        *error = 0;
    if (index) {
        *error = peIndex;
        return nullptr;
    }
    auto val = map->view.get<catsyn::IObject>(key);
    if (!val) {
        *error = peUnset;
        return nullptr;
    }
    auto func = val.try_query<const catsyn::IFunction>().clone();
    if (!func) {
        *error = peType;
        return nullptr;
    }
    return new VSFuncRef{std::move(func)};
}

int propSetNode(VSMap* map, const char* key, VSNodeRef* node, int append) noexcept {
    if (append == paTouch)
        return 0;
    if (append == paAppend) {
        auto table = map->get_mut().table;
        auto ref = table->get_ref(key);
        if (ref != catsyn::ITable::npos) {
            for (unsigned i = 1; ; ++i) {
                auto nk = format_c("__{}{}", key, i);
                ref = table->get_ref(nk);
                if (ref == catsyn::ITable::npos) {
                    map->get_mut().set(nk, node->substrate.get());
                    return 0;
                }
            }
        }
    }
    map->get_mut().set(key, node->substrate.get());
    return 0;
}

int propSetFrame(VSMap* map, const char* key, const VSFrameRef* f, int append) noexcept {
    if (append == paTouch)
        return 0;
    if (append == paAppend && map->view.get<catsyn::IObject>(key)) {
        logMessage(mtWarning, "Metalloporphyrin: paAppend not supported (propSetFrame)");
        return 1;
    }
    map->get_mut().set(key, f->frame.get());
    return 0;
}

int propSetFunc(VSMap* map, const char* key, VSFuncRef* func, int append) noexcept {
    if (append == paTouch)
        return 0;
    if (append == paAppend && map->view.get<catsyn::IObject>(key)) {
        logMessage(mtWarning, "Metalloporphyrin: paAppend not supported (propSetFunc)");
        return 1;
    }
    map->get_mut().set(key, func->func.get());
    return 0;
}
