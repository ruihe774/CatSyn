#include <VapourSynth.h>
#include <internalfilters.h>

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin* plugin) noexcept {
    resizeInitialize(configFunc, registerFunc, plugin);
}
