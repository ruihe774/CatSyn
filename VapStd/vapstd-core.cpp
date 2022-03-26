#include <VapourSynth.h>
#include <internalfilters.h>

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin* plugin) noexcept {
    configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 0, plugin);
    exprInitialize(configFunc, registerFunc, plugin);
    genericInitialize(configFunc, registerFunc, plugin);
    lutInitialize(configFunc, registerFunc, plugin);
    mergeInitialize(configFunc, registerFunc, plugin);
    reorderInitialize(configFunc, registerFunc, plugin);
    stdlibInitialize(configFunc, registerFunc, plugin);
}
