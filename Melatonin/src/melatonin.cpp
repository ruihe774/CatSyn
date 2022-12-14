#include <optional>
#include <string_view>
#include <type_traits>

#include <lua.hpp>

#include <cathelper.h>
#include <catsyn_1.h>
#include <tatabox.h>

using namespace catsyn;

#define MAKE_TNAME(cls) ("catsyn::" #cls)

template<typename... Args>
[[noreturn]] static void error(lua_State* L, fmt::format_string<Args...> fmt, Args&&... args) {
    lua_pushstring(L, format_c(std::move(fmt), std::forward<Args>(args)...));
    lua_error(L);
}

static int format_field_replace(lua_State* L);

template<typename T> static void set(lua_State* L, T val, const char* key, int idx = -1);

template<class> inline constexpr bool dependent_false_v = false;

template<typename T> static void push(lua_State* L, T val) {
    if constexpr (std::is_enum_v<T>)
        push(L, static_cast<std::underlying_type_t<T>>(val));
    else if constexpr (std::is_same_v<T, bool>)
        lua_pushboolean(L, val);
    else if constexpr (std::is_integral_v<T>)
        lua_pushinteger(L, static_cast<lua_Integer>(val));
    else if constexpr (std::is_floating_point_v<T>)
        lua_pushnumber(L, static_cast<lua_Number>(val));
    else if constexpr (std::is_function_v<std::remove_pointer_t<T>>)
        lua_pushcfunction(L, val);
    else if constexpr (std::is_same_v<T, FrameFormat>) {
        lua_createtable(L, 0, 9);
        luaL_setmetatable(L, MAKE_TNAME(FrameFormat));
        set(L, val.id, "id");
        set(L, val.detail.color_family, "color_family");
        set(L, val.detail.sample_type, "sample_type");
        set(L, val.detail.bits_per_sample, "bits_per_sample");
        set(L, bytes_per_sample(val), "bytes_per_sample");
        set(L, val.detail.width_subsampling, "width_subsampling");
        set(L, val.detail.height_subsampling, "height_subsampling");
        set(L, num_planes(val), "num_planes");
        set(L, format_field_replace, "replace");
    } else
        static_assert(dependent_false_v<T>, "type not supported");
}

template<typename T> static void set(lua_State* L, T val, const char* key, int idx) {
    idx = lua_absindex(L, idx);
    push(L, val);
    lua_setfield(L, idx, key);
}

template<typename T> static T pull(lua_State* L, int idx = -1) {
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
        try {
            ff.id = get<uint32_t>(L, "id", idx).value();
        } catch (std::bad_optional_access&) {
            error(L, "not a FrameFormat");
        }
        return ff;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        size_t len;
        auto s = lua_tolstring(L, idx, &len);
        return {s, len};
    } else
        static_assert(dependent_false_v<T>, "type not supported");
}

template<typename T> static std::optional<T> get(lua_State* L, const char* key, int idx = -1) {
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
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        std::string_view key;
        int idx = -1;
        if (lua_isnumber(L, -2))
            idx = pull<int>(L, -2);
        else
            key = pull<std::string_view>(L, -2);
        if (key == "color_family" || idx == 1)
            ff.detail.color_family = pull<ColorFamily>(L);
        else if (key == "sample_type" || idx == 2)
            ff.detail.sample_type = pull<SampleType>(L);
        else if (key == "bits_per_sample" || idx == 3)
            ff.detail.bits_per_sample = pull<unsigned>(L);
        else if (key == "width_subsampling" || idx == 4)
            ff.detail.width_subsampling = pull<unsigned>(L);
        else if (key == "height_subsampling" || idx == 5)
            ff.detail.height_subsampling = pull<unsigned>(L);
        else
            error(L, "unknown parameter '{}'", key);
        lua_pop(L, 1);
    }
    push(L, ff);
    return 1;
}

static int make_ff(lua_State* L) {
    FrameFormat ff;
    ff.id = static_cast<uint32_t>(-1);
    push(L, ff);
    lua_insert(L, 1);
    format_field_replace(L);
    ff = pull<FrameFormat>(L);
    if (static_cast<unsigned>(ff.detail.color_family) == 0xFu)
        error(L, "missing parameter 'color_family'");
    if (static_cast<unsigned>(ff.detail.sample_type) == 0xFu)
        error(L, "missing parameter 'sample_type'");
    if (ff.detail.bits_per_sample == 0xFFu)
        error(L, "missing parameter 'bits_per_sample'");
    if (ff.detail.width_subsampling == 0xFFu)
        error(L, "missing parameter 'width_subsampling'");
    if (ff.detail.height_subsampling == 0xFFu)
        error(L, "missing parameter 'height_subsampling'");
    push(L, ff);
    return 1;
}

static int ff_eq(lua_State* L) {
    push(L, pull<FrameFormat>(L, 1).id == pull<FrameFormat>(L, 2).id);
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

static void init_format_lib(lua_State* L) {
    luaL_newmetatable(L, MAKE_TNAME(FrameFormat));
    set(L, ff_eq, "__eq");
    lua_pop(L, 1);

    for (auto pd : predefined_formats)
        set(L, pd.format, pd.name);

    set(L, ColorFamily::YUV, "YUV");
    set(L, ColorFamily::RGB, "RGB");
    set(L, ColorFamily::Gray, "GRAY");
    set(L, SampleType::Integer, "INTEGER");
    set(L, SampleType::Float, "FLOAT");

    set(L, make_ff, "make_frame_format");
}

extern "C" CAT_EXPORT int luaopen_melatonin(lua_State* L) {
    lua_newtable(L);
    init_format_lib(L);
    return 1;
}
