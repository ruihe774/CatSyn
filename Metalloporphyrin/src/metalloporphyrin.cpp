#include <algorithm>
#include <shared_mutex>
#include <thread>

#include <stdio.h>

#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>

#include <VapourSynth.h>
#include <cathelper.h>
#include <catsyn.h>

class Object : virtual public catsyn::IObject {
  private:
    void drop() noexcept final {
        delete this;
    }
};

static std::vector<std::unique_ptr<VSCore>> cores;
static std::shared_mutex cores_mutex;

static VSMessageType loglevel_to_msgtype(catsyn::LogLevel level) {
    switch (level) {
    case catsyn::LogLevel::DEBUG:
    case catsyn::LogLevel::INFO:
        return mtDebug;
    case catsyn::LogLevel::WARNING:
        return mtWarning;
    default:
        throw std::logic_error("unknown log level");
    }
}

struct UserLogSink final : Object, catsyn::ILogSink, catsyn::IRef {
    struct HandlerInstance {
        VSMessageHandler handler;
        VSMessageHandlerFree freer;
        void* userData;
        int id;
        ~HandlerInstance() {
            if (freer)
                freer(userData);
        }
    };

    boost::container::small_vector<HandlerInstance, 1> handlers;

    void send_log(catsyn::LogLevel level, const char* msg) noexcept final {
        auto mt = loglevel_to_msgtype(level);
        std::shared_lock<std::shared_mutex> lock(cores_mutex);
        for (const auto& handler : handlers)
            handler.handler(mt, msg, handler.userData);
    }
};

static UserLogSink sink;

struct VSCore {
    catsyn::cat_ptr<catsyn::INucleus> nucl;
    VSCoreInfo ci;
};

static void registerFormats() noexcept;

static VSCore* createCore(int threads) noexcept {
    static bool initialized = false;
    if (!initialized) {
        registerFormats();
        initialized = true;
    }
    catsyn::cat_ptr<catsyn::INucleus> nucl;
    catsyn::create_nucleus(nucl.put());
    auto core = std::make_unique<VSCore>(
        VSCore{std::move(nucl), VSCoreInfo{"CatSyn 1.0.0 (Catalyzed video processing framework)\n"
                                           "Metalloporphyrin 1.0.0 (VapourSynth compatibility layer for CatSyn)\n"
                                           "Copyright (c) 2022 Misaki Kasumi\n"
                                           "Simulating VapourSynth Core R54, API 3.6\n",
                                           54, VAPOURSYNTH_API_VERSION, 0, 0, 0}});
    if (threads > 0) {
        auto cfg = core->nucl->get_config();
        cfg.thread_count = threads;
        core->nucl->set_config(cfg);
    }
    auto pcore = core.get();
    std::unique_lock<std::shared_mutex> lock(cores_mutex);
    if (!sink.handlers.empty())
        core->nucl->get_logger()->set_sink(&sink);
    cores.emplace_back(std::move(core));
    return pcore;
}

[[noreturn]] static void invalid_core_pointer() {
    throw std::runtime_error("invalid VSCore pointer");
}

static void freeCore(VSCore* core) noexcept {
    std::unique_lock<std::shared_mutex> lock(cores_mutex);
    if (auto it = std::find_if(cores.begin(), cores.end(), [core](const auto& p) { return p.get() == core; });
        it != cores.end())
        cores.erase(it);
    else
        invalid_core_pointer();
}

static const VSCoreInfo* getCoreInfo(VSCore* core) noexcept {
    auto cfg = core->nucl->get_config();
    core->ci.numThreads = static_cast<int>(cfg.thread_count);
    core->ci.maxFramebufferSize = static_cast<int64_t>(cfg.mem_hint_mb) << 20;
    return &core->ci;
}

static void getCoreInfo2(VSCore* core, VSCoreInfo* info) noexcept {
    *info = *getCoreInfo(core);
}

static int64_t setMaxCacheSize(int64_t bytes, VSCore* core) noexcept {
    auto cfg = core->nucl->get_config();
    cfg.mem_hint_mb = bytes >> 20;
    core->nucl->set_config(cfg);
    return bytes;
}

static void setMessageHandler(VSMessageHandler handler, void* userData) noexcept {
    std::unique_lock<std::shared_mutex> cores_lock(cores_mutex);
    sink.handlers.clear();
    if (handler) {
        sink.handlers.push_back(UserLogSink::HandlerInstance{handler, nullptr, userData, 0});
        for (auto& core : cores)
            core->nucl->get_logger()->set_sink(&sink);
    } else
        for (auto& core : cores)
            core->nucl->get_logger()->set_sink(nullptr);
}

static int addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void* userData) noexcept {
    std::unique_lock<std::shared_mutex> cores_lock(cores_mutex);
    if (handler) {
        auto empty = sink.handlers.empty();
        auto id = empty ? 0 : sink.handlers.back().id + 1;
        sink.handlers.push_back(UserLogSink::HandlerInstance{handler, free, userData, id});
        if (empty)
            for (auto& core : cores)
                core->nucl->get_logger()->set_sink(&sink);
        return id;
    } else {
        sink.handlers.clear();
        for (auto& core : cores)
            core->nucl->get_logger()->set_sink(nullptr);
        return -1;
    }
}

static int removeMessageHandler(int id) noexcept {
    std::unique_lock<std::shared_mutex> cores_lock(cores_mutex);
    if (auto it = std::find_if(sink.handlers.begin(), sink.handlers.end(),
                               [id](const auto& handler) { return handler.id == id; });
        it != sink.handlers.end()) {
        sink.handlers.erase(it);
        return 1;
    } else
        return 0;
}

static catsyn::LogLevel msgtype_to_loglevel(int mt, const char* msg) {
    switch (mt) {
    case mtDebug:
        return catsyn::LogLevel::DEBUG;
    case mtWarning:
    case mtCritical:
        return catsyn::LogLevel::WARNING;
    case mtFatal:
        throw std::runtime_error(msg);
    default:
        throw std::logic_error("unknown message type");
    }
}

static void logMessage(int mt, const char* msg) noexcept {
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    if (cores.empty())
        fprintf(stderr, "%s\n", msg);
    else
        cores.front()->nucl->get_logger()->log(msgtype_to_loglevel(mt, msg), msg);
}

static int setThreadCount(int threads, VSCore* core) noexcept {
    auto cfg = core->nucl->get_config();
    cfg.thread_count = threads > 0 ? threads : std::thread::hardware_concurrency();
    core->nucl->set_config(cfg);
}

static boost::container::flat_map<uint32_t, std::unique_ptr<VSFormat>> formats;
static std::shared_mutex formats_mutex;

static catsyn::FrameFormat ff_vs_to_cs(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW,
                                       int subSamplingH) {
    catsyn::FrameFormat ff;
    if (colorFamily > cmYUV)
        throw std::logic_error("unimplemented color family");
    ff.detail.color_family = static_cast<catsyn::ColorFamily>(colorFamily / 1000000);
    ff.detail.sample_type = static_cast<catsyn::SampleType>(sampleType);
    ff.detail.bits_per_sample = bitsPerSample;
    ff.detail.width_subsampling = subSamplingW;
    ff.detail.height_subsampling = subSamplingH;
    return ff;
}

static const VSFormat* registerFormat(catsyn::FrameFormat ff, const char* name = "unknown", int id = 0) noexcept {
    static int id_offset = 1000;
    auto ffid = ff.id;
    {
        std::shared_lock<std::shared_mutex> lock(formats_mutex);
        if (auto it = formats.find(ffid); it != formats.end())
            return it->second.get();
    }
    {
        auto colorFamily = static_cast<int>(ff.detail.color_family) * 1000000;
        auto sampleType = static_cast<int>(ff.detail.sample_type);
        VSFormat format;
        strcpy_s(format.name, sizeof(VSFormat::name), name);
        format.id = id ? id : colorFamily + id_offset++;
        format.colorFamily = colorFamily;
        format.sampleType = sampleType;
        format.bitsPerSample = static_cast<int>(ff.detail.bits_per_sample);
        format.bytesPerSample = static_cast<int>(catsyn::bytes_per_sample(ff));
        format.subSamplingW = static_cast<int>(ff.detail.width_subsampling);
        format.subSamplingH = static_cast<int>(ff.detail.height_subsampling);
        format.numPlanes = static_cast<int>(catsyn::num_planes(ff));
        std::unique_lock<std::shared_mutex> lock(formats_mutex);
        return formats.emplace(ffid, std::make_unique<VSFormat>(format)).first->second.get();
    }
}

static const VSFormat* registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW,
                                      int subSamplingH, const char* name = "unknown", int id = 0) noexcept {
    return registerFormat(ff_vs_to_cs(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH), name, id);
}

static const VSFormat* registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW,
                                      int subSamplingH, VSCore*) noexcept {
    return registerFormat(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH);
}

static void registerFormats() noexcept {
    registerFormat(cmGray, stInteger, 8, 0, 0, "Gray8", pfGray8);
    registerFormat(cmGray, stInteger, 16, 0, 0, "Gray16", pfGray16);
    registerFormat(cmGray, stFloat, 16, 0, 0, "GrayH", pfGrayH);
    registerFormat(cmGray, stFloat, 32, 0, 0, "GrayS", pfGrayS);
    registerFormat(cmYUV, stInteger, 8, 1, 1, "YUV420P8", pfYUV420P8);
    registerFormat(cmYUV, stInteger, 8, 1, 0, "YUV422P8", pfYUV422P8);
    registerFormat(cmYUV, stInteger, 8, 0, 0, "YUV444P8", pfYUV444P8);
    registerFormat(cmYUV, stInteger, 8, 2, 2, "YUV410P8", pfYUV410P8);
    registerFormat(cmYUV, stInteger, 8, 2, 0, "YUV411P8", pfYUV411P8);
    registerFormat(cmYUV, stInteger, 8, 0, 1, "YUV440P8", pfYUV440P8);
    registerFormat(cmYUV, stInteger, 9, 1, 1, "YUV420P9", pfYUV420P9);
    registerFormat(cmYUV, stInteger, 9, 1, 0, "YUV422P9", pfYUV422P9);
    registerFormat(cmYUV, stInteger, 9, 0, 0, "YUV444P9", pfYUV444P9);
    registerFormat(cmYUV, stInteger, 10, 1, 1, "YUV420P10", pfYUV420P10);
    registerFormat(cmYUV, stInteger, 10, 1, 0, "YUV422P10", pfYUV422P10);
    registerFormat(cmYUV, stInteger, 10, 0, 0, "YUV444P10", pfYUV444P10);
    registerFormat(cmYUV, stInteger, 12, 1, 1, "YUV420P12", pfYUV420P12);
    registerFormat(cmYUV, stInteger, 12, 1, 0, "YUV422P12", pfYUV422P12);
    registerFormat(cmYUV, stInteger, 12, 0, 0, "YUV444P12", pfYUV444P12);
    registerFormat(cmYUV, stInteger, 14, 1, 1, "YUV420P14", pfYUV420P14);
    registerFormat(cmYUV, stInteger, 14, 1, 0, "YUV422P14", pfYUV422P14);
    registerFormat(cmYUV, stInteger, 14, 0, 0, "YUV444P14", pfYUV444P14);
    registerFormat(cmYUV, stInteger, 16, 1, 1, "YUV420P16", pfYUV420P16);
    registerFormat(cmYUV, stInteger, 16, 1, 0, "YUV422P16", pfYUV422P16);
    registerFormat(cmYUV, stInteger, 16, 0, 0, "YUV444P16", pfYUV444P16);
    registerFormat(cmYUV, stFloat, 16, 0, 0, "YUV444PH", pfYUV444PH);
    registerFormat(cmYUV, stFloat, 32, 0, 0, "YUV444PS", pfYUV444PS);
    registerFormat(cmRGB, stInteger, 8, 0, 0, "RGB24", pfRGB24);
    registerFormat(cmRGB, stInteger, 9, 0, 0, "RGB27", pfRGB27);
    registerFormat(cmRGB, stInteger, 10, 0, 0, "RGB30", pfRGB30);
    registerFormat(cmRGB, stInteger, 16, 0, 0, "RGB48", pfRGB48);
    registerFormat(cmRGB, stFloat, 16, 0, 0, "RGBH", pfRGBH);
    registerFormat(cmRGB, stFloat, 32, 0, 0, "RGBS", pfRGBS);
}

struct VSFrameRef {
    catsyn::cat_ptr<catsyn::IFrame> frame;
};

static VSFrameRef* newVideoFrame(const VSFormat* format, int width, int height, const VSFrameRef* propSrc,
                                 VSCore* core) noexcept {
    catsyn::FrameInfo fi{
        ff_vs_to_cs(format->colorFamily, format->sampleType, format->bitsPerSample, format->subSamplingW,
                    format->subSamplingH),
        static_cast<unsigned>(width),
        static_cast<unsigned>(height),
    };
    auto frame_ref = new VSFrameRef;
    core->nucl->get_factory()->create_frame(fi, nullptr, nullptr, propSrc ? propSrc->frame->get_frame_props() : nullptr,
                                            frame_ref->frame.put());
    return frame_ref;
}

static VSFrameRef* newVideoFrame2(const VSFormat* format, int width, int height, const VSFrameRef** planeSrc,
                                  const int* planes, const VSFrameRef* propSrc, VSCore* core) noexcept {
    if (!planeSrc || !planes)
        return newVideoFrame(format, width, height, propSrc, core);
    catsyn::FrameInfo fi{
        ff_vs_to_cs(format->colorFamily, format->sampleType, format->bitsPerSample, format->subSamplingW,
                    format->subSamplingH),
        static_cast<unsigned>(width),
        static_cast<unsigned>(height),
    };
    auto frame_ref = new VSFrameRef;
    std::array<const catsyn::IAlignedBytes*, 3> agb;
    std::array<size_t, 3> strides;
    for (unsigned i = 0; i < catsyn::num_planes(fi.format); ++i)
        if (planeSrc[i]) {
            auto frame = planeSrc[i]->frame;
            agb[i] = frame->get_plane(planes[i]);
            strides[i] = frame->get_stride(planes[i]);
        } else
            agb[i] = nullptr;
    core->nucl->get_factory()->create_frame(
        fi, agb.data(), strides.data(), propSrc ? propSrc->frame->get_frame_props() : nullptr, frame_ref->frame.put());
    return frame_ref;
}

static VSFrameRef* copyFrame(const VSFrameRef* f, VSCore*) noexcept {
    return new VSFrameRef{f->frame.clone()};
}

static const VSFrameRef* cloneFrameRef(const VSFrameRef* f) noexcept {
    return new VSFrameRef{f->frame};
}

static void freeFrame(const VSFrameRef* f) noexcept {
    delete f;
}

static int getStride(const VSFrameRef* f, int plane) noexcept {
    return static_cast<int>(f->frame->get_stride(plane));
}

static const uint8_t* getReadPtr(const VSFrameRef* f, int plane) noexcept {
    return static_cast<const uint8_t*>(f->frame->get_plane(plane)->data());
}

static uint8_t* getWritePtr(VSFrameRef* f, int plane) noexcept {
    return static_cast<uint8_t*>(f->frame->get_plane_mut(plane)->data());
}

static const VSFormat* getFrameFormat(const VSFrameRef* f) noexcept {
    return registerFormat(f->frame->get_frame_info().format);
}

static int getFrameWidth(const VSFrameRef* f, int plane) noexcept {
    return static_cast<int>(catsyn::plane_width(f->frame->get_frame_info(), plane));
}

static int getFrameHeight(const VSFrameRef* f, int plane) noexcept {
    return static_cast<int>(catsyn::plane_height(f->frame->get_frame_info(), plane));
}

static void copyFrameProps(const VSFrameRef* src, VSFrameRef* dst, VSCore*) noexcept {
    catsyn::cat_ptr<const catsyn::ITable> props = src->frame->get_frame_props();
    dst->frame->set_frame_props(props.clone().get());
}
struct VSMap {
    catsyn::TableView<const catsyn::ITable> table;
    bool mut;

    explicit VSMap(catsyn::ITable* table) noexcept : table(table), mut(true) {}
    explicit VSMap(const catsyn::ITable* table) noexcept : table(table), mut(false) {}

    catsyn::TableView<catsyn::ITable>& get_mut() {
        if (!mut)
            throw std::runtime_error("attempt to modify an immutable table");
        return reinterpret_cast<catsyn::TableView<catsyn::ITable>&>(table);
    }
};

static const VSMap* getFramePropsRO(const VSFrameRef* f) noexcept {
    return new VSMap(f->frame->get_frame_props());
}

static VSMap* getFramePropsRW(VSFrameRef* f) noexcept {
    return new VSMap(f->frame->get_frame_props_mut());
}

[[noreturn]] static void no_core() {
    throw std::runtime_error("no core");
}

static VSMap *createMap() noexcept {
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    if (cores.empty())
        no_core();
    else {
        catsyn::cat_ptr<catsyn::ITable> table;
        cores.front()->nucl->get_factory()->create_table(0, table.put());
        return new VSMap(table.get());
    }
}

static void freeMap(VSMap *map) noexcept {
    delete map;
}

static void clearMap(VSMap *map) noexcept {
    map->get_mut().table->resize(0);
}

static void setError(VSMap *map, const char *errorMessage) noexcept {
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    if (cores.empty())
        no_core();
    else {
        catsyn::cat_ptr<catsyn::IBytes> bytes;
        cores.front()->nucl->get_factory()->create_bytes(errorMessage, strlen(errorMessage) + 1, bytes.put());
        map->get_mut().set("__error", bytes.get());
    }
}

static const char *getError(const VSMap *map) noexcept {
    return static_cast<const char *>(map->table.get<catsyn::IBytes>("__error")->data());
}

static int propNumKeys(const VSMap *map) noexcept {
    return static_cast<int>(map->table.size());
}

static const char *propGetKey(const VSMap *map, int index) noexcept {
    return map->table.table->get_key(index);
}

static int propDeleteKey(VSMap *map, const char *key) noexcept {
    map->get_mut().del(key);
}

static char propGetType(const VSMap *map, const char *key) noexcept {
    auto val = map->table.get<catsyn::IObject>(key);
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

static int propNumElements(const VSMap *map, const char *key) noexcept {
    auto val = map->table.get<catsyn::IObject>(key);
    if (!val)
        return -1;
    if (auto arr = val.try_query<const catsyn::INumberArray>(); arr)
        return arr->size() / 8;
    else
        return 1;
}

static const catsyn::INumberArray* propGetIntArrayImpl(const VSMap *map, const char *key, int *error) noexcept {
    auto val = map->table.get<catsyn::IObject>(key);
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

static const int64_t *propGetIntArray(const VSMap *map, const char *key, int *error) noexcept {
    auto arr = propGetIntArrayImpl(map, key, error);
    return arr ? static_cast<const int64_t *>(arr->data()) : nullptr;
}

static const catsyn::INumberArray* propGetFloatArrayImpl(const VSMap *map, const char *key, int *error) noexcept {
    auto val = map->table.get<catsyn::IObject>(key);
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

static const double* propGetFloatArray(const VSMap *map, const char *key, int *error) noexcept {
    auto arr = propGetFloatArrayImpl(map, key, error);
    return arr ? static_cast<const double*>(arr->data()) : nullptr;
}

static int64_t propGetInt(const VSMap *map, const char *key, int index, int *error) noexcept {
    auto arr = propGetIntArrayImpl(map, key, error);
    if (!arr)
        return 0;
    if (index >= arr->size() / 8) {
        *error = peIndex;
        return 0;
    }
    return static_cast<const int64_t*>(arr->data())[index];
}

static double propGetFloat(const VSMap *map, const char *key, int index, int *error) noexcept {
    auto arr = propGetFloatArrayImpl(map, key, error);
    if (!arr)
        return 0;
    if (index >= arr->size() / 8) {
        *error = peIndex;
        return 0;
    }
    return static_cast<const double*>(arr->data())[index];
}

static const catsyn::IBytes *propGetDataImpl(const VSMap *map, const char *key, int index, int *error) noexcept {
    if (index) {
        *error = peIndex;
        return nullptr;
    }
    auto val = map->table.get<catsyn::IObject>(key);
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

static const char *propGetData(const VSMap *map, const char *key, int index, int *error) noexcept {
    auto data = propGetDataImpl(map, key, index, error);
    return data ? static_cast<const char*>(data->data()) : nullptr;
}

static int propGetDataSize(const VSMap *map, const char *key, int index, int *error) noexcept {
    return static_cast<int>(propGetDataImpl(map, key, index, error)->size() - 1);
}
