#include <lua.hpp>

#include <catsyn_1.h>
#include <cathelper.h>

using namespace catsyn;

#define MAKE_TNAME(cls) ("catsyn::" #cls)

struct NamedFormat {
    const char* name;
    FrameFormat format;
};

static const NamedFormat predefined_formats[] = {
    {"GRAY8", make_frame_format(ColorFamily::Gray, SampleType::Integer, 8, 0, 0)},
    {"GRAY10", make_frame_format(ColorFamily::Gray, SampleType::Integer, 10, 0, 0)},
    {"GRAY16", make_frame_format(ColorFamily::Gray, SampleType::Integer, 16, 0, 0)},
    {"GRAYS", make_frame_format(ColorFamily::Gray, SampleType::Float, 32, 0, 0)},
    {"YUV420P8", make_frame_format(ColorFamily::YUV, SampleType::Integer, 8, 1, 1)},
    {"YUV420P10", make_frame_format(ColorFamily::YUV, SampleType::Integer, 10, 1, 1)},
    {"YUV420P16", make_frame_format(ColorFamily::YUV, SampleType::Integer, 16, 1, 1)},
    {"YUV420PS", make_frame_format(ColorFamily::YUV, SampleType::Float, 32, 1, 1)},
    {"YUV444P8", make_frame_format(ColorFamily::YUV, SampleType::Integer, 8, 0, 0)},
    {"YUV444P10", make_frame_format(ColorFamily::YUV, SampleType::Integer, 10, 0, 0)},
    {"YUV444P16", make_frame_format(ColorFamily::YUV, SampleType::Integer, 16, 0, 0)},
    {"YUV444PS", make_frame_format(ColorFamily::YUV, SampleType::Float, 32, 0, 0)},
    {"RGB24", make_frame_format(ColorFamily::RGB, SampleType::Integer, 8, 0, 0)},
    {"RGB30", make_frame_format(ColorFamily::RGB, SampleType::Integer, 10, 0, 0)},
    {"RGB48", make_frame_format(ColorFamily::RGB, SampleType::Integer, 16, 0, 0)},
    {"RGBS", make_frame_format(ColorFamily::RGB, SampleType::Float, 32, 0, 0)},
};

static void register_formats(lua_State* L) noexcept {
    luaL_newmetatable(L, MAKE_TNAME(FrameFormat));
    lua_pop(L, 1);
    for (auto pd : predefined_formats) {
        lua_newtable(L);
        luaL_setmetatable(L, MAKE_TNAME(FrameFormat));
        lua_pushinteger(L, pd.format.id);
        lua_setfield(L, -2, "id");
        lua_setfield(L, -2, pd.name);
    }
}

extern "C" CAT_EXPORT int luaopen_melatonin(lua_State* L) {
    lua_newtable(L);
    register_formats(L);
    return 1;
}
