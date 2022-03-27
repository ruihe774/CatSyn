#pragma once

#include <map>
#include <optional>
#include <shared_mutex>
#include <vector>

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

extern VSAPI api;
extern std::unique_ptr<VSCore> core;

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

struct VSPlugin {
    catsyn::cat_ptr<catsyn::IEnzyme> enzyme;
    std::map<std::string, std::string> arg_strs;
};

struct VSCore {
    catsyn::cat_ptr<catsyn::INucleus> nucl;
    VSCoreInfo ci;
    std::map<std::string_view, std::unique_ptr<VSPlugin>> plugins;
    std::map<std::string_view, VSPlugin*> ns_map;
    std::shared_mutex plugins_mutex;
};

VSCore* createCore(int threads) noexcept;
void freeCore(VSCore* core) noexcept;
const VSCoreInfo* getCoreInfo(VSCore* core) noexcept;
void getCoreInfo2(VSCore* core, VSCoreInfo* info) noexcept;
int64_t setMaxCacheSize(int64_t bytes, VSCore* core) noexcept;
int setThreadCount(int threads, VSCore* core) noexcept;

struct VSFrameRef {
    catsyn::cat_ptr<const catsyn::IFrame> frame;
    catsyn::cat_ptr<catsyn::IFrame>& get_mut() noexcept;
};

const VSFormat* registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH,
                               VSCore*) noexcept;
const VSFormat* registerFormat(catsyn::FrameFormat ff, const char* name = "unknown", int id = 0) noexcept;
const VSFormat* getFormatPreset(int id, VSCore*) noexcept;
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
    catsyn::cat_ptr<const catsyn::ITable> table;
    explicit VSMap(const catsyn::ITable* table) noexcept;
    explicit VSMap(catsyn::cat_ptr<const catsyn::ITable> table) noexcept;
    catsyn::ITable* get_mut() noexcept;
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
VSNodeRef* propGetNode(const VSMap* map, const char* key, int index, int* error) noexcept;
const VSFrameRef* propGetFrame(const VSMap* map, const char* key, int index, int* error) noexcept;
VSFuncRef* propGetFunc(const VSMap* map, const char* key, int index, int* error) noexcept;
int propSetNode(VSMap* map, const char* key, VSNodeRef* node, int append) noexcept;
int propSetFrame(VSMap* map, const char* key, const VSFrameRef* f, int append) noexcept;
int propSetFunc(VSMap* map, const char* key, VSFuncRef* func, int append) noexcept;

struct VSNodeRef {
    catsyn::cat_ptr<catsyn::ISubstrate> substrate;
    catsyn::cat_ptr<catsyn::IOutput> output;
    VSVideoInfo vi;
};

VSNodeRef* cloneNodeRef(VSNodeRef* node) noexcept;
void freeNode(VSNodeRef* node) noexcept;
const VSFrameRef* getFrame(int n, VSNodeRef* node, char* errorMsg, int bufSize) noexcept;
void getFrameAsync(int n, VSNodeRef* node, VSFrameDoneCallback callback, void* userData) noexcept;
const VSFrameRef* getFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept;
void requestFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept;
const VSVideoInfo* getVideoInfo(VSNodeRef* node) noexcept;
void setVideoInfo(const VSVideoInfo* vi, int numOutputs, VSNode* node) noexcept;

struct VSFunc final : Object, catsyn::IFunction {
    VSPublicFunction func;
    void* userData;
    VSFreeFuncData freer;
    std::optional<std::vector<catsyn::ArgSpec>> specs;

    VSFunc(const VSFunc&) = delete;
    VSFunc(VSFunc&&) = delete;
    VSFunc(VSPublicFunction func, void* userData, VSFreeFuncData freer,
           std::optional<std::vector<catsyn::ArgSpec>> specs = std::nullopt) noexcept;
    ~VSFunc() final;

    void invoke(catsyn::ITable* args, const IObject** out) final;
    const catsyn::ArgSpec* get_arg_specs(size_t* len) const noexcept final;
    const std::type_info* get_out_type() const noexcept final;
};

struct VSFuncRef {
    catsyn::cat_ptr<catsyn::IFunction> func;
};

VSFuncRef* createFunc(VSPublicFunction func, void* userData, VSFreeFuncData free, VSCore* core,
                      const VSAPI* vsapi) noexcept;
VSFuncRef* cloneFuncRef(VSFuncRef* f) noexcept;
void callFunc(VSFuncRef* func, const VSMap* in, VSMap* out, VSCore* core, const VSAPI* vsapi) noexcept;
void freeFunc(VSFuncRef* f) noexcept;

void registerFunction(const char* name, const char* args, VSPublicFunction argsFunc, void* functionData,
                      VSPlugin* plugin) noexcept;
VSPlugin* getPluginById(const char* identifier, VSCore* core) noexcept;
VSPlugin* getPluginByNs(const char* ns, VSCore* core) noexcept;
VSMap* getPlugins(VSCore* core) noexcept;
VSMap* getFunctions(VSPlugin* plugin) noexcept;

catsyn::FrameFormat ff_vs_to_cs(const VSFormat* vsf);
void createFilter(const VSMap* in, VSMap* out, const char* name, VSFilterInit init, VSFilterGetFrame getFrame,
                  VSFilterFree freer, int filterMode, int flags, void* instanceData, VSCore* core) noexcept;
void setFilterError(const char* errorMessage, VSFrameContext* frameCtx) noexcept;

VSMap* invoke(VSPlugin* plugin, const char* name, const VSMap* args) noexcept;
void queryCompletedFrame(VSNodeRef** node, int* n, VSFrameContext* frameCtx) noexcept;
void releaseFrameEarly(VSNodeRef* node, int n, VSFrameContext* frameCtx) noexcept;
int getOutputIndex(VSFrameContext* frameCtx) noexcept;
const char* getPluginPath(const VSPlugin* plugin) noexcept;

struct VSRibosome final : Object, catsyn::IRibosome {
    const char* get_identifier() const noexcept final;
    void synthesize_enzyme(const char* token, IObject** out) noexcept final;
    void hydrolyze_enzyme(IObject** inout) noexcept final;
};

extern bool force_single_threaded_for_this_filter;
