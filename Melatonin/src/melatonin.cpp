#include <type_traits>
#include <optional>

#include <lua.hpp>

#include <catsyn_1.h>
#include <cathelper.h>

using namespace catsyn;

#define MAKE_TNAME(cls) ("catsyn::" #cls)

[[noreturn]] static void error(lua_State* L, const char* msg) {
    luaL_error(L, msg);
}

static int format_field_replace(lua_State* L);

template<typename T>
static void set(lua_State* L, T val, const char* key, int idx = -2);

template<typename T>
static void push(lua_State* L, T val) {
    if constexpr (std::is_enum_v<T>)
        push(L, static_cast<std::underlying_type_t<T>>(val));
    else if constexpr (std::is_integral_v<T>)
        lua_pushinteger(L, static_cast<lua_Integer>(val));
    else if constexpr (std::is_floating_point_v<T>)
        lua_pushnumber(L, static_cast<lua_Number>(val));
    else if constexpr (std::is_function_v<std::remove_pointer_t<T>>)
        lua_pushcfunction(L, val);
    else if constexpr (std::is_same_v<T, FrameFormat>) {
        lua_createtable(L, 0, 8);
        luaL_setmetatable(L, MAKE_TNAME(FrameFormat));
        set(L, val.id, "id");
        set(L, val.detail.color_family, "color_family");
        set(L, val.detail.sample_type, "sample_type");
        set(L, val.detail.bits_per_sample, "bits_per_sample");
        set(L, bytes_per_sample(val), "bytes_per_sample");
        set(L, val.detail.width_subsampling, "width_subsampling");
        set(L, val.detail.height_subsampling, "height_subsampling");
        set(L, format_field_replace, "replace");
    }
}

template<typename T>
static void set(lua_State* L, T val, const char* key, int idx) {
    push(L, val);
    lua_setfield(L, idx, key);
}

template<typename T>
static T pull(lua_State* L, int idx = -1) {
    if constexpr (std::is_enum_v<T>)
        return static_cast<T>(pull<std::underlying_type_t<T>>(L, idx));
    else if constexpr (std::is_integral_v<T>) {
        int isnum;
        auto val = lua_tointegerx(L, idx, &isnum);
        if (!isnum) [[unlikely]]
            error(L, "not a integer");
        return static_cast<T>(val);
    } else if constexpr (std::is_floating_point_v<T>) {
        int isnum;
        auto val = lua_tonumberx(L, idx, &isnum);
        if (!isnum) [[unlikely]]
            error(L, "not a number");
        return static_cast<T>(val);
    } else if constexpr (std::is_same_v<T, FrameFormat>) {
        FrameFormat ff;
        ff.id = get<uint32_t>(L, "id", idx).value();
        return ff;
    }
}

template<typename T>
static std::optional<T> get(lua_State* L, const char* key, int idx) {
    lua_getfield(L, idx, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::nullopt;
    }
    T val = pull<T>(L, -1);
    lua_pop(L, 1);
    return std::make_optional(val);
}

static int format_field_replace(lua_State* L) {
    auto ff = pull<FrameFormat>(L, 1);
    if (auto cc = get<ColorFamily>(L, "color_family", 2); cc)
        ff.detail.color_family = cc.value();
    if (auto st = get<SampleType>(L, "sample_type", 2); st)
        ff.detail.sample_type = st.value();
    if (auto bits = get<unsigned>(L, "bits_per_sample", 2); bits)
        ff.detail.bits_per_sample = bits.value();
    if (auto bytes = get<unsigned>(L, "bytes_per_sample", 2); bytes)
        error(L, "replace bytes_per_sample not supported; use bits_per_sample instead");
    if (auto sw = get<unsigned>(L, "width_subsampling", 2); sw)
        ff.detail.width_subsampling = sw.value();
    if (auto sh = get<unsigned>(L, "height_subsampling", 2); sh)
        ff.detail.height_subsampling = sh.value();
    push(L, ff);
    return 1;
}

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

static void register_formats(lua_State* L) {
    luaL_newmetatable(L, MAKE_TNAME(FrameFormat));
    lua_pop(L, 1);
    for (auto pd : predefined_formats)
        set(L, pd.format, pd.name);
}

extern "C" CAT_EXPORT int luaopen_melatonin(lua_State* L) {
    lua_newtable(L);
    register_formats(L);
    return 1;
}
