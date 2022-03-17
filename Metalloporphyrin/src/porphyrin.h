#pragma once

#include <shared_mutex>

#include <boost/container/small_vector.hpp>

#include <cathelper.h>
#include <catsyn.h>

#define VS_CORE_EXPORTS
#include <VapourSynth.h>

#include <metalcfg.h>

class Object : virtual public catsyn::IObject {
  private:
    void drop() noexcept final {
        delete this;
    }
};

struct UserLogSink final : Object, catsyn::ILogSink, catsyn::IRef {
    struct HandlerInstance {
        VSMessageHandler handler;
        VSMessageHandlerFree freer;
        void* userData;
        int id;
        ~HandlerInstance();
        HandlerInstance(const HandlerInstance&) = delete;
        HandlerInstance(HandlerInstance&&) noexcept;
        HandlerInstance(VSMessageHandler handler, VSMessageHandlerFree freer, void* userData, int id) noexcept;
        HandlerInstance& operator=(const HandlerInstance&) = delete;
        HandlerInstance& operator=(HandlerInstance&&) noexcept;
    };

    boost::container::small_vector<HandlerInstance, 1> handlers;

    void send_log(catsyn::LogLevel level, const char* msg) noexcept final;
};

extern UserLogSink sink;

void setMessageHandler(VSMessageHandler handler, void* userData) noexcept;
int addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void* userData) noexcept;
int removeMessageHandler(int id) noexcept;
void logMessage(int mt, const char* msg) noexcept;

struct VSCore {
    catsyn::cat_ptr<catsyn::INucleus> nucl;
    VSCoreInfo ci;
};

extern std::vector<std::unique_ptr<VSCore>> cores;
extern std::shared_mutex cores_mutex;

VSCore* createCore(int threads) noexcept;
void freeCore(VSCore* core) noexcept;
const VSCoreInfo* getCoreInfo(VSCore* core) noexcept;
void getCoreInfo2(VSCore* core, VSCoreInfo* info) noexcept;
int64_t setMaxCacheSize(int64_t bytes, VSCore* core) noexcept;
int setThreadCount(int threads, VSCore* core) noexcept;

const VSFormat* registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH,
                               VSCore*) noexcept;
VSFrameRef* newVideoFrame(const VSFormat* format, int width, int height, const VSFrameRef* propSrc,
                          VSCore* core) noexcept;
VSFrameRef* newVideoFrame2(const VSFormat* format, int width, int height, const VSFrameRef** planeSrc,
                           const int* planes, const VSFrameRef* propSrc, VSCore* core) noexcept;
VSFrameRef* copyFrame(const VSFrameRef* f, VSCore*) noexcept;
const VSFrameRef* cloneFrameRef(const VSFrameRef* f) noexcept;
void freeFrame(const VSFrameRef* f) noexcept;
int getStride(const VSFrameRef* f, int plane) noexcept;
const uint8_t* getReadPtr(const VSFrameRef* f, int plane) noexcept;
uint8_t* getWritePtr(VSFrameRef* f, int plane) noexcept;
const VSFormat* getFrameFormat(const VSFrameRef* f) noexcept;
int getFrameWidth(const VSFrameRef* f, int plane) noexcept;
int getFrameHeight(const VSFrameRef* f, int plane) noexcept;
void copyFrameProps(const VSFrameRef* src, VSFrameRef* dst, VSCore*) noexcept;
const VSMap* getFramePropsRO(const VSFrameRef* f) noexcept;
VSMap* getFramePropsRW(VSFrameRef* f) noexcept;

struct VSMap {
    catsyn::TableView<const catsyn::ITable> view;
    bool mut;

    explicit VSMap(catsyn::ITable* table) noexcept;
    explicit VSMap(const catsyn::ITable* table) noexcept;

    catsyn::TableView<catsyn::ITable>& get_mut();
};

VSMap* createMap() noexcept;
void freeMap(VSMap* map) noexcept;
void clearMap(VSMap* map) noexcept;
void setError(VSMap* map, const char* errorMessage) noexcept;
const char* getError(const VSMap* map) noexcept;
int propNumKeys(const VSMap* map) noexcept;
const char* propGetKey(const VSMap* map, int index) noexcept;
int propDeleteKey(VSMap* map, const char* key) noexcept;
char propGetType(const VSMap* map, const char* key) noexcept;
int propNumElements(const VSMap* map, const char* key) noexcept;
const int64_t* propGetIntArray(const VSMap* map, const char* key, int* error) noexcept;
const double* propGetFloatArray(const VSMap* map, const char* key, int* error) noexcept;
int64_t propGetInt(const VSMap* map, const char* key, int index, int* error) noexcept;
double propGetFloat(const VSMap* map, const char* key, int index, int* error) noexcept;
const char* propGetData(const VSMap* map, const char* key, int index, int* error) noexcept;
int propGetDataSize(const VSMap* map, const char* key, int index, int* error) noexcept;
int propSetInt(VSMap* map, const char* key, int64_t i, int append) noexcept;
int propSetIntArray(VSMap* map, const char* key, const int64_t* i, int size) noexcept;
int propSetFloat(VSMap* map, const char* key, double i, int append) noexcept;
int propSetFloatArray(VSMap* map, const char* key, const double* i, int size) noexcept;
int propSetData(VSMap* map, const char* key, const char* data, int size, int append) noexcept;
