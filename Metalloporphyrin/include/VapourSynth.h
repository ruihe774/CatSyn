#pragma once

#ifndef __cplusplus
#error "we only support C++"
#endif

#include <stdint.h>

#define VAPOURSYNTH_API_MAJOR 3
#define VAPOURSYNTH_API_MINOR 6
#define VAPOURSYNTH_API_VERSION ((VAPOURSYNTH_API_MAJOR << 16) | (VAPOURSYNTH_API_MINOR))

#define VS_CC
#ifdef _WIN32
#define VS_EXTERNAL_API(ret) extern "C" __declspec(dllexport) ret
#else
#define VS_EXTERNAL_API(ret) extern "C" __attribute__((visibility("default"))) ret
#endif

#if !defined(VS_CORE_EXPORTS) && defined(_WIN32)
#define VS_API(ret) extern "C" __declspec(dllimport) ret
#else
#define VS_API(ret) VS_EXTERNAL_API(ret)
#endif

struct VSFrameRef;
struct VSNodeRef;
struct VSCore;
struct VSPlugin;
struct VSNode;
struct VSFuncRef;
struct VSMap;
struct VSAPI;
struct VSFrameContext;

enum VSColorFamily {
    cmGray = 1000000,
    cmRGB = 2000000,
    cmYUV = 3000000,
    cmYCoCg = 4000000,
    cmCompat = 9000000
};

enum VSSampleType {
    stInteger = 0,
    stFloat = 1
};

enum VSPresetFormat {
    pfNone = 0,
    pfGray8 = cmGray + 10,
    pfGray16,
    pfGrayH,
    pfGrayS,
    pfYUV420P8 = cmYUV + 10,
    pfYUV422P8,
    pfYUV444P8,
    pfYUV410P8,
    pfYUV411P8,
    pfYUV440P8,
    pfYUV420P9,
    pfYUV422P9,
    pfYUV444P9,
    pfYUV420P10,
    pfYUV422P10,
    pfYUV444P10,
    pfYUV420P16,
    pfYUV422P16,
    pfYUV444P16,
    pfYUV444PH,
    pfYUV444PS,
    pfYUV420P12,
    pfYUV422P12,
    pfYUV444P12,
    pfYUV420P14,
    pfYUV422P14,
    pfYUV444P14,
    pfRGB24 = cmRGB + 10,
    pfRGB27,
    pfRGB30,
    pfRGB48,
    pfRGBH,
    pfRGBS,
    pfCompatBGR32 = cmCompat + 10,
    pfCompatYUY2
};

enum VSFilterMode {
    fmParallel = 100,
    fmParallelRequests = 200,
    fmUnordered = 300,
    fmSerial = 400
};

struct VSFormat {
    char name[32];
    int id;
    int colorFamily;
    int sampleType;
    int bitsPerSample;
    int bytesPerSample;
    int subSamplingW;
    int subSamplingH;
    int numPlanes;
};

enum VSNodeFlags {
    nfNoCache = 1,
    nfIsCache = 2,
    nfMakeLinear = 4
};

enum VSPropTypes {
    ptUnset = 'u',
    ptInt = 'i',
    ptFloat = 'f',
    ptData = 's',
    ptNode = 'c',
    ptFrame = 'v',
    ptFunction = 'm'
};

enum VSGetPropErrors {
    peUnset = 1,
    peType = 2,
    peIndex = 4
};

enum VSPropAppendMode {
    paReplace = 0,
    paAppend = 1,
    paTouch = 2
};

struct VSCoreInfo {
    const char* versionString;
    int core;
    int api;
    int numThreads;
    int64_t maxFramebufferSize;
    int64_t usedFramebufferSize;
};

struct VSVideoInfo {
    const VSFormat* format;
    int64_t fpsNum;
    int64_t fpsDen;
    int width;
    int height;
    int numFrames;
    int flags;
};

enum VSActivationReason {
    arInitial = 0,
    arFrameReady = 1,
    arAllFramesReady = 2,
    arError = -1
};

enum VSMessageType {
    mtDebug = 0,
    mtWarning = 1,
    mtCritical = 2,
    mtFatal = 3
};

typedef const VSAPI* (*VSGetVapourSynthAPI)(int version);
typedef void (*VSPublicFunction)(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
typedef void (*VSRegisterFunction)(const char* name, const char* args, VSPublicFunction argsFunc, void* functionData,
                                   VSPlugin* plugin);
typedef void (*VSConfigPlugin)(const char* identifier, const char* defaultNamespace, const char* name, int apiVersion,
                               int readonly, VSPlugin* plugin);
typedef void (*VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin* plugin);
typedef void (*VSFreeFuncData)(void* userData);
typedef void (*VSFilterInit)(VSMap* in, VSMap* out, void** instanceData, VSNode* node, VSCore* core,
                             const VSAPI* vsapi);
typedef const VSFrameRef* (*VSFilterGetFrame)(int n, int activationReason, void** instanceData, void** frameData,
                                              VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi);
typedef void (*VSFilterFree)(void* instanceData, VSCore* core, const VSAPI* vsapi);
typedef void (*VSFrameDoneCallback)(void* userData, const VSFrameRef* f, int n, VSNodeRef*, const char* errorMsg);
typedef void (*VSMessageHandler)(int msgType, const char* msg, void* userData);
typedef void (*VSMessageHandlerFree)(void* userData);

struct VSAPI {
    VSCore* (*createCore)(int threads) noexcept;
    void (*freeCore)(VSCore* core) noexcept;
    const VSCoreInfo* (*getCoreInfo)(VSCore* core) noexcept;
    const VSFrameRef* (*cloneFrameRef)(const VSFrameRef* f) noexcept;
    VSNodeRef* (*cloneNodeRef)(VSNodeRef* node) noexcept;
    VSFuncRef* (*cloneFuncRef)(VSFuncRef* f) noexcept;
    void (*freeFrame)(const VSFrameRef* f) noexcept;
    void (*freeNode)(VSNodeRef* node) noexcept;
    void (*freeFunc)(VSFuncRef* f) noexcept;
    VSFrameRef* (*newVideoFrame)(const VSFormat* format, int width, int height, const VSFrameRef* propSrc,
                                 VSCore* core) noexcept;
    VSFrameRef* (*copyFrame)(const VSFrameRef* f, VSCore* core) noexcept;
    void (*copyFrameProps)(const VSFrameRef* src, VSFrameRef* dst, VSCore* core) noexcept;
    void (*registerFunction)(const char* name, const char* args, VSPublicFunction argsFunc, void* functionData,
                             VSPlugin* plugin) noexcept;
    VSPlugin* (*getPluginById)(const char* identifier, VSCore* core) noexcept;
    VSPlugin* (*getPluginByNs)(const char* ns, VSCore* core) noexcept;
    VSMap* (*getPlugins)(VSCore* core) noexcept;
    VSMap* (*getFunctions)(VSPlugin* plugin) noexcept;
    void (*createFilter)(const VSMap* in, VSMap* out, const char* name, VSFilterInit init, VSFilterGetFrame getFrame,
                         VSFilterFree free, int filterMode, int flags, void* instanceData, VSCore* core) noexcept;
    void (*setError)(VSMap* map, const char* errorMessage) noexcept;
    const char* (*getError)(const VSMap* map) noexcept;
    void (*setFilterError)(const char* errorMessage, VSFrameContext* frameCtx) noexcept;
    VSMap* (*invoke)(VSPlugin* plugin, const char* name, const VSMap* args) noexcept;
    const VSFormat* (*getFormatPreset)(int id, VSCore* core) noexcept;
    const VSFormat* (*registerFormat)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW,
                                      int subSamplingH, VSCore* core) noexcept;
    const VSFrameRef* (*getFrame)(int n, VSNodeRef* node, char* errorMsg, int bufSize) noexcept;
    void (*getFrameAsync)(int n, VSNodeRef* node, VSFrameDoneCallback callback, void* userData) noexcept;
    const VSFrameRef* (*getFrameFilter)(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept;
    void (*requestFrameFilter)(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept;
    void (*queryCompletedFrame)(VSNodeRef** node, int* n, VSFrameContext* frameCtx) noexcept;
    void (*releaseFrameEarly)(VSNodeRef* node, int n, VSFrameContext* frameCtx) noexcept;
    int (*getStride)(const VSFrameRef* f, int plane) noexcept;
    const uint8_t* (*getReadPtr)(const VSFrameRef* f, int plane) noexcept;
    uint8_t* (*getWritePtr)(VSFrameRef* f, int plane) noexcept;
    VSFuncRef* (*createFunc)(VSPublicFunction func, void* userData, VSFreeFuncData free, VSCore* core,
                             const VSAPI* vsapi) noexcept;
    void (*callFunc)(VSFuncRef* func, const VSMap* in, VSMap* out, VSCore* core, const VSAPI* vsapi) noexcept;
    VSMap* (*createMap)() noexcept;
    void (*freeMap)(VSMap* map) noexcept;
    void (*clearMap)(VSMap* map) noexcept;
    const VSVideoInfo* (*getVideoInfo)(VSNodeRef* node) noexcept;
    void (*setVideoInfo)(const VSVideoInfo* vi, int numOutputs, VSNode* node) noexcept;
    const VSFormat* (*getFrameFormat)(const VSFrameRef* f) noexcept;
    int (*getFrameWidth)(const VSFrameRef* f, int plane) noexcept;
    int (*getFrameHeight)(const VSFrameRef* f, int plane) noexcept;
    const VSMap* (*getFramePropsRO)(const VSFrameRef* f) noexcept;
    VSMap* (*getFramePropsRW)(VSFrameRef* f) noexcept;
    int (*propNumKeys)(const VSMap* map) noexcept;
    const char* (*propGetKey)(const VSMap* map, int index) noexcept;
    int (*propNumElements)(const VSMap* map, const char* key) noexcept;
    char (*propGetType)(const VSMap* map, const char* key) noexcept;
    int64_t (*propGetInt)(const VSMap* map, const char* key, int index, int* error) noexcept;
    double (*propGetFloat)(const VSMap* map, const char* key, int index, int* error) noexcept;
    const char* (*propGetData)(const VSMap* map, const char* key, int index, int* error) noexcept;
    int (*propGetDataSize)(const VSMap* map, const char* key, int index, int* error) noexcept;
    VSNodeRef* (*propGetNode)(const VSMap* map, const char* key, int index, int* error) noexcept;
    const VSFrameRef* (*propGetFrame)(const VSMap* map, const char* key, int index, int* error) noexcept;
    VSFuncRef* (*propGetFunc)(const VSMap* map, const char* key, int index, int* error) noexcept;
    int (*propDeleteKey)(VSMap* map, const char* key) noexcept;
    int (*propSetInt)(VSMap* map, const char* key, int64_t i, int append) noexcept;
    int (*propSetFloat)(VSMap* map, const char* key, double d, int append) noexcept;
    int (*propSetData)(VSMap* map, const char* key, const char* data, int size, int append) noexcept;
    int (*propSetNode)(VSMap* map, const char* key, VSNodeRef* node, int append) noexcept;
    int (*propSetFrame)(VSMap* map, const char* key, const VSFrameRef* f, int append) noexcept;
    int (*propSetFunc)(VSMap* map, const char* key, VSFuncRef* func, int append) noexcept;
    int64_t (*setMaxCacheSize)(int64_t bytes, VSCore* core) noexcept;
    int (*getOutputIndex)(VSFrameContext* frameCtx) noexcept;
    VSFrameRef* (*newVideoFrame2)(const VSFormat* format, int width, int height, const VSFrameRef** planeSrc,
                                  const int* planes, const VSFrameRef* propSrc, VSCore* core) noexcept;
    void (*setMessageHandler)(VSMessageHandler handler, void* userData) noexcept;
    int (*setThreadCount)(int threads, VSCore* core) noexcept;
    const char* (*getPluginPath)(const VSPlugin* plugin) noexcept;
    const int64_t* (*propGetIntArray)(const VSMap* map, const char* key, int* error) noexcept;
    const double* (*propGetFloatArray)(const VSMap* map, const char* key, int* error) noexcept;
    int (*propSetIntArray)(VSMap* map, const char* key, const int64_t* i, int size) noexcept;
    int (*propSetFloatArray)(VSMap* map, const char* key, const double* d, int size) noexcept;
    void (*logMessage)(int msgType, const char* msg) noexcept;
    int (*addMessageHandler)(VSMessageHandler handler, VSMessageHandlerFree free, void* userData) noexcept;
    int (*removeMessageHandler)(int id) noexcept;
    void (*getCoreInfo2)(VSCore* core, VSCoreInfo* info) noexcept;
};

VS_API(const VSAPI*) getVapourSynthAPI(int version) noexcept;
