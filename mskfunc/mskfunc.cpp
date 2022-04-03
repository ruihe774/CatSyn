#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <boost/container/small_vector.hpp>

#include <Shlwapi.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <dxgi.h>

#include <wil/com.h>

#include <lua.hpp>

#if defined(_M_IX86_FP) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "lowway.h"

#include "run_text_template.h"
#include "transform_lib.h"

[[nodiscard]] std::wstring u2w(std::string_view s) {
    static_assert(sizeof(wchar_t) == 2, "wchar_t required to be 2 bytes");
    if (s.empty())
        return {};
    wchar_t short_buf[2048];
    auto len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), short_buf, sizeof(short_buf) / 2);
    if (len)
        return {short_buf, static_cast<std::size_t>(len)};
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        THROW_LAST_ERROR();
    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    THROW_LAST_ERROR_IF(len == 0);
    std::unique_ptr<wchar_t[]> buf(new wchar_t[len]);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), buf.get(), len);
    return {buf.get(), static_cast<std::size_t>(len)};
}

[[nodiscard]] std::string w2u(std::wstring_view s) {
    static_assert(sizeof(char) == 1, "char required to be 1 byte");
    if (s.empty())
        return {};
    char short_buf[4096];
    auto len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), short_buf, sizeof(short_buf), nullptr, nullptr);
    if (len)
        return {short_buf, static_cast<std::size_t>(len)};
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        THROW_LAST_ERROR();
    len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    THROW_LAST_ERROR_IF(len == 0);
    std::unique_ptr<char[]> buf(new char[len]);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), buf.get(), len, nullptr, nullptr);
    return {buf.get(), static_cast<std::size_t>(len)};
}

[[nodiscard]] wil::com_ptr<IDWriteFactory6> get_dwrite_factory() {
    static wil::com_ptr<IDWriteFactory6> factory;
    if (!factory)
        THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6), factory.put_unknown()));
    return factory;
}

[[nodiscard]] wil::com_ptr<ID2D1Factory> get_d2d1_factory() {
    static wil::com_ptr<ID2D1Factory> factory;
    if (!factory)
        THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
    return factory;
}

[[nodiscard]] wil::com_ptr<ID2D1DeviceContext5> get_d2d1_device_context() {
    static wil::com_ptr<ID2D1DeviceContext5> dc;
    if (!dc) {
        wil::com_ptr<ID3D11Device> d3d11_device;
        THROW_IF_FAILED(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_NULL, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            d3d11_device.put(), nullptr, nullptr));
        auto dxgi_device = d3d11_device.query<IDXGIDevice>();
        auto d2d1_factory = get_d2d1_factory().query<ID2D1Factory6>();
        wil::com_ptr<ID2D1Device5> d2d1_device;
        THROW_IF_FAILED(d2d1_factory->CreateDevice(dxgi_device.get(), d2d1_device.put()));
        THROW_IF_FAILED(d2d1_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc.put()));
    }
    return dc;
}

struct Point {
    float x;
    float y;
};

enum class PathOp {
    MoveTo,
    LinesTo,
    BeziersTo,
    Close,
    End,
};

struct Path {
    typedef boost::container::small_vector<Point, 3> PointVec;
    PathOp op;
    PointVec points;
};

struct Transform {
    float m11, m12, m21, m22, dx, dy;

  public:
    [[nodiscard]] Point transform_point(Point point) const { return {point.x * m11 + point.y * m21 + dx, point.x * m12 + point.y * m22 + dy}; }

    void transform_points(const Point* begin, const Point* end, Point* dest) const {
        if (end <= begin)
            return;
        m128 mt = mm_load128(&m11);
        m128 m1 = mm_shuffle(mt, mt, 0, 1, 0, 1);
        m128 m2 = mm_shuffle(mt, mt, 2, 3, 2, 3);
        m128 dt = mm_load64(&dx);
        dt = mm_shuffle(dt, dt, 0, 1, 0, 1);
#       pragma clang loop unroll_count(2)
        while (begin + 1 < end) {
            m128 pt = mm_load128(begin);
            m128 p1 = mm_shuffle(pt, pt, 0, 0, 2, 2);
            m128 p2 = mm_shuffle(pt, pt, 1, 1, 3, 3);
            m128 r1 = mm_fmadd(p1, m1, dt);
            m128 r2 = mm_fmadd(p2, m2, r1);
            mm_store128(dest, r2);
            begin += 2;
            dest += 2;
        }
        if (begin != end) {
            m128 pt = mm_load64(begin);
            m128 p1 = mm_shuffle(pt, pt, 0, 0, -1, -1);
            m128 p2 = mm_shuffle(pt, pt, 1, 1, -1, -1);
            m128 r1 = mm_fmadd(p1, m1, dt);
            m128 r2 = mm_fmadd(p2, m2, r1);
            mm_store64(dest, r2);
        }
    }

    [[nodiscard]] static Transform identity() { return Transform {1, 0, 0, 1, 0, 0}; }
};

Transform operator*(const Transform& a, const Transform& b) {
    Transform m;
    m128 ma0 = mm_load128(&a);                     // ma0 = [a.m11, a.m12, a.m21, a.m22]
    m128 ma1 = mm_shuffle(ma0, ma0, 0, 0, 2, 2);    // ma1 = [a.m11, a.m11, a.m21, a.m21]
    m128 ma2 = mm_shuffle(ma0, ma0, 1, 1, 3, 3);    // ma2 = [a.m12, a.m12, a.m22, a.m22]
    m128 mb0 = mm_load128(&b);                     // mb0 = [b.m11, b.m12, b.m21, b.m22]
    m128 mb1 = mm_shuffle(mb0, mb0, 0, 1, 0, 1);    // mb1 = [b.m11, b.m12, b.m11, b.m12]
    m128 mb2 = mm_shuffle(mb0, mb0, 2, 3, 2, 3);    // mb2 = [b.m21, b.m22, b.m21, b.m22]
    m128 r1 = mm_mul(ma1, mb1);
    m128 r2 = mm_fmadd(ma2, mb2, r1);
    mm_store128(&m, r2);
    m128 db0 = mm_load64(&b.dx);                    // db0 = [b.dx, b.dy]
    m128 da0 = mm_load64(&a.dx);                    // da0 = [a.dx, a.dy]
    m128 da1 = mm_shuffle(da0, da0, 0, 0, -1, -1);  // da1 = [a.dx, a.dx]
    m128 da2 = mm_shuffle(da0, da0, 1, 1, -1, -1);  // da2 = [a.dy, a.dy]
    r1 = mm_fmadd(da1, mb1, db0);
    r2 = mm_fmadd(da2, mb2, r1);
    mm_store64(&m.dx, r2);
    return m;
}

class Shape;

struct Box {
    float left, top, width, height;

    [[nodiscard]] Shape to_mask() const;
};

class Shape {
    std::vector<Path> _paths;
    mutable wil::com_ptr<ID2D1Geometry> _cache;

    [[nodiscard]] const std::vector<Path>& paths() const { return _paths; }

    [[nodiscard]] std::vector<Path>& paths_mut() {
        _cache.reset();
        return _paths;
    }

    void reserve(std::size_t cap) {
        _paths.reserve(cap);
    }

  public:
    [[nodiscard]] const Path& get(std::size_t idx) const { return paths()[idx]; }

    void set(std::size_t idx, Path path) { paths_mut()[idx] = std::move(path); }

    [[nodiscard]] std::size_t size() const { return paths().size(); }

    void append(Path path) { paths_mut().emplace_back(std::move(path)); }

    void append_auto_merge(Path path) {
        auto& paths = paths_mut();
        if (paths.empty() || paths.back().op != path.op)
            append(std::move(path));
        else {
            auto& points = paths.back().points;
            points.insert(points.end(), path.points.begin(), path.points.end());
        }
    }

    void extend(const Shape& other) { paths_mut().insert(paths_mut().end(), other.paths().begin(), other.paths().end()); }

    void clear() { paths_mut().clear(); }

    [[nodiscard]] bool empty() const { return size() == 0; }

    [[nodiscard]] Shape subset(std::size_t begin, std::size_t end) const {
        Shape sub;
        auto& sub_path = sub.paths_mut();
        auto it = paths().begin();
        sub_path.insert(sub_path.end(), it + static_cast<decltype(it)::difference_type>(begin), it + static_cast<decltype(it)::difference_type>(end));
        return sub;
    }

    [[nodiscard]] decltype(auto) begin() const { return paths().begin(); }

    [[nodiscard]] decltype(auto) end() const { return paths().end(); }

    [[nodiscard]] std::string tostring(int decimal_places) const;

    [[nodiscard]] Shape transform(const Transform& matrix) const {
        Shape trans_shape;
        trans_shape.reserve(size());
        for (auto& path : *this) {
            auto& points = path.points;
            Path::PointVec trans_points {points.size()};
            matrix.transform_points(points.data(), points.data() + points.size(), trans_points.data());
            trans_shape.append(Path {path.op, std::move(trans_points)});
        }
        return trans_shape;
    }

    [[nodiscard]] Shape filter_open() const {
        Shape filtered;
        filtered.reserve(size());
        for (auto& path : *this)
            if (path.op != PathOp::End)
                filtered.append(path);
        return filtered;
    }

    [[nodiscard]] std::optional<Box> minmax() const {
        float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
        for (auto& path : *this) {
            auto [min_x_it, max_x_it] = std::minmax_element(path.points.begin(), path.points.end(), [](Point a, Point b) { return a.x < b.x; });
            auto [min_y_it, max_y_it] = std::minmax_element(path.points.begin(), path.points.end(), [](Point a, Point b) { return a.y < b.y; });
            min_x = std::min(min_x, min_x_it->x);
            min_y = std::min(min_y, min_y_it->y);
            max_x = std::max(max_x, max_x_it->x);
            max_y = std::max(max_y, max_y_it->y);
        }
        if (min_x > max_x || min_y > max_y)
            return {};
        return Box {min_x, min_y, max_x - min_x, max_y - min_y};
    }

  private:
    void stream_to_sink(ID2D1SimplifiedGeometrySink* sink, bool close_sink = true) const {
        bool figure_in_progress = false;
        for (auto& path : *this) {
            switch (path.op) {
                case PathOp::MoveTo:
                    if (figure_in_progress)
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->BeginFigure(*reinterpret_cast<const D2D1_POINT_2F*>(path.points.data()), D2D1_FIGURE_BEGIN_FILLED);
                    figure_in_progress = true;
                    break;
                case PathOp::Close:
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    figure_in_progress = false;
                    break;
                case PathOp::LinesTo:
                    sink->AddLines(reinterpret_cast<const D2D1_POINT_2F*>(path.points.data()), static_cast<UINT32>(path.points.size()));
                    break;
                case PathOp::BeziersTo:
                    sink->AddBeziers(reinterpret_cast<const D2D1_BEZIER_SEGMENT*>(path.points.data()), static_cast<UINT32>(path.points.size() / 3));
                    break;
                case PathOp::End:
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    figure_in_progress = false;
                    break;
            }
        }
        if (figure_in_progress)
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (close_sink)
            THROW_IF_FAILED(sink->Close());
    }

    [[nodiscard]] wil::com_ptr<ID2D1Geometry> to_geometry() const {
        if (_cache)
            return _cache;
        auto factory = get_d2d1_factory();
        wil::com_ptr<ID2D1PathGeometry> geo;
        THROW_IF_FAILED(factory->CreatePathGeometry(geo.put()));
        wil::com_ptr<ID2D1GeometrySink> sink;
        THROW_IF_FAILED(geo->Open(sink.put()));
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        auto simplified_sink = sink.query<ID2D1SimplifiedGeometrySink>();
        stream_to_sink(simplified_sink.get());
        _cache = geo.query<ID2D1Geometry>();
        return _cache;
    }

    [[nodiscard]] wil::com_ptr<ID2D1SimplifiedGeometrySink> open_sink(bool ignore_open = false);

    void load_from_geometry(ID2D1Geometry* geo) {
        THROW_IF_FAILED(geo->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, nullptr, open_sink().get()));
    }

  public:
    [[nodiscard]] Shape anchor(Box box) const;
    [[nodiscard]] std::optional<Box> bounds() const;

    enum class CombineMode {
        UNION = D2D1_COMBINE_MODE_UNION,
        INTERSECT = D2D1_COMBINE_MODE_INTERSECT,
        XOR = D2D1_COMBINE_MODE_XOR,
        EXCLUDE = D2D1_COMBINE_MODE_EXCLUDE,
    };

    [[nodiscard]] Shape combine(const Shape& other, CombineMode mode, float tolerance) const;

    enum class Relation {
        DISJOINT = D2D1_GEOMETRY_RELATION_DISJOINT,
        IS_CONTAINED = D2D1_GEOMETRY_RELATION_IS_CONTAINED,
        CONTAINS = D2D1_GEOMETRY_RELATION_CONTAINS,
        OVERLAP = D2D1_GEOMETRY_RELATION_OVERLAP,
    };

    [[nodiscard]] Relation compare(const Shape& other, float tolerance) const;

    [[nodiscard]] bool contains(Point point, float tolerance) const;
    [[nodiscard]] float compute_area(float tolerance) const;
    [[nodiscard]] float compute_length(float tolerance) const;

    struct PointDescription {
        Point point;
        Point tangent;
        std::size_t path_idx;
        std::size_t figure_idx;
        float length_to_path;
    };

    [[nodiscard]] PointDescription point_at_length(float length, std::size_t start_path_idx, float tolerance) const;

    [[nodiscard]] Shape flatten(float tolerance) const;
    [[nodiscard]] Shape outline(float tolerance) const;

    struct StrokeStyle {
        enum class LineCap {
            FLAT = D2D1_CAP_STYLE_FLAT,
            SQUARE = D2D1_CAP_STYLE_SQUARE,
            ROUND = D2D1_CAP_STYLE_ROUND,
            TRIANGLE = D2D1_CAP_STYLE_TRIANGLE,
        };
        enum class LineJoin {
            MITER = D2D1_LINE_JOIN_MITER,
            BEVEL = D2D1_LINE_JOIN_BEVEL,
            ROUND = D2D1_LINE_JOIN_ROUND,
            MITER_OR_BEVEL = D2D1_LINE_JOIN_MITER_OR_BEVEL,
        };
        enum class DashStyle {
            SOLID = D2D1_DASH_STYLE_SOLID,
            DASH = D2D1_DASH_STYLE_DASH,
            DOT = D2D1_DASH_STYLE_DOT,
            DASH_DOT = D2D1_DASH_STYLE_DASH_DOT,
            DASH_DOT_DOT = D2D1_DASH_STYLE_DASH_DOT_DOT,
            CUSTOM = D2D1_DASH_STYLE_CUSTOM,
        };
        float width = 1.f;
        LineCap start_cap = LineCap::FLAT;
        LineCap end_cap = LineCap::FLAT;
        LineCap dash_cap = LineCap::FLAT;
        LineJoin line_join = LineJoin::MITER_OR_BEVEL;
        float miter_limit = 4.f;
        DashStyle dash_style = DashStyle::SOLID;
        std::vector<float> dash_pattern;
        float dash_offset = 0.f;
    };

    [[nodiscard]] Shape widen(const StrokeStyle& stroke, float tolerance) const;
    [[nodiscard]] Shape widen1(const StrokeStyle& stroke, float tolerance, const Transform& transform) const;

    friend class TextRenderer;
    friend class Svg;
};

[[nodiscard]] Shape Box::to_mask() const {
    Shape mask;
    mask.append(Path {PathOp::MoveTo, {Point {left, top}}});
    mask.append(Path {
        PathOp::LinesTo,
        {
            Point {left + width, top},
            Point {left + width, top + height},
            Point {left, top + height},
        }});
    return mask;
}

char op2char(PathOp op) {
    switch (op) {
        case PathOp::MoveTo: return 'm';
        case PathOp::LinesTo: return 'l';
        case PathOp::BeziersTo: return 'b';
        case PathOp::Close: return 'c';
        case PathOp::End: return 'e';
    }
    __assume(false); // stupid msvc
}

PathOp char2op(char ch) {
    switch (ch) {
        case 'm': return PathOp::MoveTo;
        case 'l': return PathOp::LinesTo;
        case 'b': return PathOp::BeziersTo;
        case 'c': return PathOp::Close;
        case 'e': return PathOp::End;
        default: throw std::invalid_argument("invalid path op");
    }
}

class StringBuilder {
    std::string buf;

  public:
    [[nodiscard]] const std::string& str() const { return buf; }

    [[nodiscard]] std::string& str() { return buf; }

    StringBuilder& operator<<(std::string_view s) {
        buf.append(s);
        return *this;
    }

    StringBuilder& operator<<(char ch) {
        buf.push_back(ch);
        return *this;
    }

    [[nodiscard]] std::string&& into_str() { return std::move(buf); }
};

#if defined(_M_IX86_FP) || defined(_M_X64)
int inline_roundf(float v) {
    return _mm_cvt_ss2si(_mm_set_ss(v));
}
#else
int inline_roundf(float v) {
    return lroundf(v);
}
#endif

#define STR_ADP_IMPLICIT_TOSTRING operator std::string() const { StringBuilder os; os << *this; return os.into_str(); }

class FixedF {
    float v;
    int decimal_places;

  public:
    FixedF(float v, int decimal_places): v(v), decimal_places(decimal_places) {}

    friend StringBuilder& operator<<(StringBuilder&, FixedF);

    STR_ADP_IMPLICIT_TOSTRING
};

StringBuilder& operator<<(StringBuilder& os, FixedF ff) {
    auto [v, decimal_places] = ff;
    const std::array<float, 8> exp10_table = {1.f, 10.f, 100.f, 1000.f, 10000.f, 100000.f, 1000000.f, 10000000.f};
    auto iv = inline_roundf(v * exp10_table.at(decimal_places));
    if (iv == 0)
        return os << '0';
    bool neg = iv < 0;
    if (neg)
        iv = -iv;
    char buf[16];
    char* p = buf;
#   pragma clang loop unroll(disable)
    for (int i = 0; i < decimal_places; ++i) {
        *p++ = iv % 10 + 48;
        iv /= 10;
    }
    *p++ = '.';
    while (iv != 0) {
        *p++ = iv % 10 + 48;
        iv /= 10;
    }
    if (neg)
        *p++ = '-';
    char* end = p;
    char* begin = buf;
    while (*begin == '0')
        ++begin;
    if (*begin == '.')
        ++begin;
    os.str().append(std::reverse_iterator<const char*>(end), std::reverse_iterator<const char*>(begin));
    return os;
}

std::string Shape::tostring(int decimal_places) const {
    StringBuilder os;
    bool first = true;
    for (auto& path : *this) {
        if (!first)
            os << ' ';
        else
            first = false;
        os << op2char(path.op);
        for (auto [x, y] : path.points) {
            os << ' ' << FixedF(x, decimal_places) << ' ' << FixedF(y, decimal_places);
        }
    }
    return os.into_str();
}

std::istream& operator>>(std::istream& is, Shape& shape) {
    shape.clear();
    while (is) {
        char op;
        is >> op;
        Path path {char2op(op), {}};
        auto& points = path.points;
        float x, y;
        while (is >> x >> y)
            points.emplace_back(Point {x, y});
        if (is.fail() && !is.bad() && !is.eof())
            is.clear();
        shape.append(std::move(path));
    }
    return is;
}

class ShapeSink final : public ID2D1SimplifiedGeometrySink {
    std::atomic_uint32_t refcount = 0;

    Shape& shape;
    bool has_error = false;
    bool ignore_open;

  public:
    explicit ShapeSink(Shape& shape, bool ignore_open = false) : shape(shape), ignore_open(ignore_open) {}

    IFACEMETHOD_(void, BeginFigure)(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) {
        if (figureBegin != D2D1_FIGURE_BEGIN_FILLED)
            has_error = true;
        shape.append_auto_merge(Path {PathOp::MoveTo, {*reinterpret_cast<Point*>(&startPoint)}});
    }

    IFACEMETHOD_(void, AddLines)(const D2D1_POINT_2F* points, UINT32 pointsCount) {
        shape.append_auto_merge(Path {PathOp::LinesTo, {reinterpret_cast<const Point*>(points), reinterpret_cast<const Point*>(points + pointsCount)}});
    }

    IFACEMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT* beziers, UINT32 beziersCount) {
        shape.append_auto_merge(Path {PathOp::BeziersTo, {reinterpret_cast<const Point*>(beziers), reinterpret_cast<const Point*>(beziers + beziersCount)}});
    }

    IFACEMETHOD(Close)() {
        HRESULT hr = S_OK;
        if (has_error)
            hr = E_FAIL;
        has_error = false;
        return hr;
    }

    IFACEMETHOD_(void, EndFigure)(D2D1_FIGURE_END figureEnd) {
        if (figureEnd == D2D1_FIGURE_END_OPEN && !ignore_open)
            shape.append_auto_merge(Path {PathOp::End, {}});
    }

    IFACEMETHOD_(void, SetFillMode)(D2D1_FILL_MODE fillMode) {
        if (fillMode != D2D1_FILL_MODE_WINDING)
            has_error = true;
    }

    IFACEMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT) {}

    IFACEMETHOD_(unsigned long, AddRef)() { return ++refcount; }

    IFACEMETHOD_(unsigned long, Release)() {
        auto new_count = --refcount;
        if (new_count == 0)
            delete this;
        return new_count;
    }

    IFACEMETHOD(QueryInterface)(IID const& riid, void** ppvObject) {
        if (__uuidof(ID2D1SimplifiedGeometrySink) == riid || __uuidof(IUnknown) == riid)
            *ppvObject = this;
        else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
};

wil::com_ptr<ID2D1SimplifiedGeometrySink> Shape::open_sink(bool ignore_open) {
    return new ShapeSink {*this, ignore_open};
}

Shape Shape::anchor(Box box) const {
    auto anchored {box.left != 0 || box.top != 0 ? transform(Transform {1, 0, 0, 1, -box.left, -box.top}) : *this};
    if (box.width > 0 && box.height > 0) {
        anchored.append(Path {PathOp::MoveTo, {Point {0, 0}}});
        anchored.append(Path {PathOp::LinesTo, {Point {0, 0}}});
        anchored.append(Path {PathOp::MoveTo, {Point {box.width, box.height}}});
        anchored.append(Path {PathOp::LinesTo, {Point {box.width, box.height}}});
    }
    return anchored;
}

std::optional<Box> Shape::bounds() const {
    D2D1_RECT_F rect;
    THROW_IF_FAILED(to_geometry()->GetBounds(nullptr, &rect));
    if (rect.left > rect.right)
        return std::nullopt;
    return Box {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

Shape Shape::combine(const Shape& other, Shape::CombineMode mode, float tolerance) const {
    Shape combined;
    THROW_IF_FAILED(
        to_geometry()->CombineWithGeometry(other.to_geometry().get(), static_cast<D2D1_COMBINE_MODE>(mode), nullptr, tolerance, combined.open_sink(true).get()));
    return combined;
}

Shape::Relation Shape::compare(const Shape& other, float tolerance) const {
    D2D1_GEOMETRY_RELATION relation;
    THROW_IF_FAILED(to_geometry()->CompareWithGeometry(other.to_geometry().get(), nullptr, tolerance, &relation));
    return static_cast<Shape::Relation>(relation);
}

bool Shape::contains(Point point, float tolerance) const {
    BOOL contains;
    THROW_IF_FAILED(to_geometry()->FillContainsPoint(*reinterpret_cast<D2D1_POINT_2F*>(&point), nullptr, tolerance, &contains));
    return contains;
}

float Shape::compute_area(float tolerance) const {
    FLOAT area;
    THROW_IF_FAILED(to_geometry()->ComputeArea(nullptr, tolerance, &area));
    return area;
}

float Shape::compute_length(float tolerance) const {
    FLOAT length;
    THROW_IF_FAILED(to_geometry()->ComputeLength(nullptr, tolerance, &length));
    return length;
}

Shape::PointDescription Shape::point_at_length(float length, std::size_t start_path_idx, float tolerance) const {
    D2D1_POINT_DESCRIPTION description;
    THROW_IF_FAILED(
        to_geometry().query<ID2D1PathGeometry1>()->ComputePointAndSegmentAtLength(length, static_cast<UINT32>(start_path_idx), nullptr, &description));
    return {
        *reinterpret_cast<Point*>(&description.point), *reinterpret_cast<Point*>(&description.unitTangentVector), description.endSegment, description.endFigure,
        description.lengthToEndSegment};
}

Shape Shape::flatten(float tolerance) const {
    Shape flattened;
    THROW_IF_FAILED(to_geometry()->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, nullptr, tolerance, flattened.open_sink().get()));
    return flattened;
}

Shape Shape::outline(float tolerance) const {
    Shape outline;
    THROW_IF_FAILED(to_geometry()->Outline(nullptr, tolerance, outline.open_sink().get()));
    return outline;
}

Shape Shape::widen(const Shape::StrokeStyle& stroke, float tolerance) const {
    return widen1(stroke, tolerance, Transform::identity());
}

Shape Shape::widen1(const Shape::StrokeStyle& stroke, float tolerance, const Transform& transform) const {
    D2D1_STROKE_STYLE_PROPERTIES1 props {
        static_cast<D2D1_CAP_STYLE>(stroke.start_cap),
        static_cast<D2D1_CAP_STYLE>(stroke.end_cap),
        static_cast<D2D1_CAP_STYLE>(stroke.dash_cap),
        static_cast<D2D1_LINE_JOIN>(stroke.line_join),
        stroke.miter_limit,
        static_cast<D2D1_DASH_STYLE>(stroke.dash_style),
        stroke.dash_offset,
        D2D1_STROKE_TRANSFORM_TYPE_NORMAL,
    };
    wil::com_ptr<ID2D1StrokeStyle1> stroke_style;
    THROW_IF_FAILED(get_d2d1_factory().query<ID2D1Factory1>()->CreateStrokeStyle(
        props, stroke.dash_pattern.data(), static_cast<UINT32>(stroke.dash_pattern.size()), stroke_style.put()));
    Shape widened;
    THROW_IF_FAILED(
        to_geometry()->Widen(stroke.width, stroke_style.get(), reinterpret_cast<const D2D1_MATRIX_3X2_F*>(&transform), tolerance, widened.open_sink(true).get()));
    return widened;
}

struct FontFaceRetriever final : public IDWriteTextRenderer1 {
    std::atomic_uint32_t refcount = 0;

    std::vector<wil::com_ptr<IDWriteFontFace>> fontfaces;

    IFACEMETHOD(DrawGlyphRun)
    (void*, FLOAT, FLOAT, DWRITE_GLYPH_ORIENTATION_ANGLE, DWRITE_MEASURING_MODE, DWRITE_GLYPH_RUN const* glyphRun, DWRITE_GLYPH_RUN_DESCRIPTION const*,
     IUnknown*) {
        fontfaces.emplace_back(glyphRun->fontFace);
        return S_OK;
    }

    IFACEMETHOD(DrawGlyphRun)
    (void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE, DWRITE_GLYPH_RUN const* glyphRun, DWRITE_GLYPH_RUN_DESCRIPTION const*, IUnknown*) {
        fontfaces.emplace_back(glyphRun->fontFace);
        return S_OK;
    }

    IFACEMETHOD(DrawInlineObject)
    (void*, FLOAT, FLOAT, DWRITE_GLYPH_ORIENTATION_ANGLE, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) { return E_NOTIMPL; }

    IFACEMETHOD(DrawInlineObject)
    (void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) { return E_NOTIMPL; }

    IFACEMETHOD(DrawStrikethrough)
    (void*, FLOAT, FLOAT, DWRITE_GLYPH_ORIENTATION_ANGLE, DWRITE_STRIKETHROUGH const*, IUnknown*) { return E_NOTIMPL; }

    IFACEMETHOD(DrawStrikethrough)
    (void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) { return E_NOTIMPL; }

    IFACEMETHOD(DrawUnderline)
    (void*, FLOAT, FLOAT, DWRITE_GLYPH_ORIENTATION_ANGLE, DWRITE_UNDERLINE const*, IUnknown*) { return E_NOTIMPL; }

    IFACEMETHOD(DrawUnderline)
    (void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) { return E_NOTIMPL; }

    IFACEMETHOD(IsPixelSnappingDisabled)(void*, BOOL* isDisabled) {
        *isDisabled = TRUE;
        return S_OK;
    }

    IFACEMETHOD(GetCurrentTransform)(void*, DWRITE_MATRIX*) { return E_NOTIMPL; }

    IFACEMETHOD(GetPixelsPerDip)(void*, FLOAT*) { return E_NOTIMPL; }

    IFACEMETHOD_(unsigned long, AddRef)() { return ++refcount; }

    IFACEMETHOD_(unsigned long, Release)() {
        auto new_count = --refcount;
        if (new_count == 0)
            delete this;
        return new_count;
    }

    IFACEMETHOD(QueryInterface)(IID const& riid, void** ppvObject) {
        if (__uuidof(IDWriteTextRenderer1) == riid || __uuidof(IDWriteTextRenderer) == riid || __uuidof(IDWritePixelSnapping) == riid ||
            __uuidof(IUnknown) == riid)
            *ppvObject = this;
        else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
};

class TextRenderer final : public IDWriteTextRenderer1 {
    std::atomic_uint32_t refcount = 0;

    Shape& shape;

  public:
    explicit TextRenderer(Shape& shape) : shape(shape) {}

    IFACEMETHOD(DrawGlyphRun)
    (void* clientDrawingContext,
     FLOAT baselineOriginX,
     FLOAT baselineOriginY,
     DWRITE_GLYPH_ORIENTATION_ANGLE orientationAngle,
     DWRITE_MEASURING_MODE measuringMode,
     DWRITE_GLYPH_RUN const* glyphRun,
     DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
     IUnknown* clientDrawingEffect) {
        Shape current;
        auto sink = current.open_sink();

        RETURN_IF_FAILED(glyphRun->fontFace->GetGlyphRunOutline(
            glyphRun->fontEmSize,
            glyphRun->glyphIndices,
            glyphRun->glyphAdvances,
            glyphRun->glyphOffsets,
            glyphRun->glyphCount,
            glyphRun->isSideways,
            glyphRun->bidiLevel % 2 == 1,
            sink.get()));

        auto mat = D2D1::Matrix3x2F::Rotation(static_cast<float>((glyphRun->isSideways + orientationAngle) * 90));
        mat.dx = baselineOriginX;
        mat.dy = baselineOriginY;
        auto trans = current.transform(*reinterpret_cast<Transform*>(&mat));

        shape.extend(trans);

        return S_OK;
    }

    IFACEMETHOD(DrawGlyphRun)
    (void* clientDrawingContext,
     FLOAT baselineOriginX,
     FLOAT baselineOriginY,
     DWRITE_MEASURING_MODE measuringMode,
     DWRITE_GLYPH_RUN const* glyphRun,
     DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
     IUnknown* clientDrawingEffect) {
        return DrawGlyphRun(
            clientDrawingContext, baselineOriginX, baselineOriginY, DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES, measuringMode, glyphRun, glyphRunDescription,
            clientDrawingEffect);
    }

    IFACEMETHOD(DrawInlineObject)
    (void* clientDrawingContext,
     FLOAT originX,
     FLOAT originY,
     DWRITE_GLYPH_ORIENTATION_ANGLE orientationAngle,
     IDWriteInlineObject* inlineObject,
     BOOL isSideways,
     BOOL isRightToLeft,
     IUnknown* clientDrawingEffect) {
        return E_NOTIMPL;
    }

    IFACEMETHOD(DrawInlineObject)
    (void* clientDrawingContext,
     FLOAT originX,
     FLOAT originY,
     IDWriteInlineObject* inlineObject,
     BOOL isSideways,
     BOOL isRightToLeft,
     IUnknown* clientDrawingEffect) {
        return DrawInlineObject(
            clientDrawingContext, originX, originY, DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES, inlineObject, isSideways, isRightToLeft, clientDrawingEffect);
    }

    IFACEMETHOD(DrawStrikethrough)
    (void* clientDrawingContext,
     FLOAT baselineOriginX,
     FLOAT baselineOriginY,
     DWRITE_GLYPH_ORIENTATION_ANGLE orientationAngle,
     DWRITE_STRIKETHROUGH const* strikethrough,
     IUnknown* clientDrawingEffect) {
        Shape current;
        current.append(Path {PathOp::MoveTo, {Point {0, 0 + strikethrough->offset}}});
        current.append(Path {
            PathOp::LinesTo,
            {
                Point {0 + strikethrough->width, 0 + strikethrough->offset},
                Point {0 + strikethrough->width, 0 + strikethrough->offset + strikethrough->thickness},
                Point {0, 0 + strikethrough->offset + strikethrough->thickness},
            }});

        auto mat = D2D1::Matrix3x2F::Rotation(static_cast<float>(orientationAngle * 90));
        mat.dx = baselineOriginX;
        mat.dy = baselineOriginY;
        auto trans = current.transform(*reinterpret_cast<Transform*>(&mat));

        shape.extend(trans);

        return S_OK;
    }

    IFACEMETHOD(DrawStrikethrough)
    (void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_STRIKETHROUGH const* strikethrough, IUnknown* clientDrawingEffect) {
        return DrawStrikethrough(
            clientDrawingContext, baselineOriginX, baselineOriginY, DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES, strikethrough, clientDrawingEffect);
    }

    IFACEMETHOD(DrawUnderline)
    (void* clientDrawingContext,
     FLOAT baselineOriginX,
     FLOAT baselineOriginY,
     DWRITE_GLYPH_ORIENTATION_ANGLE orientationAngle,
     DWRITE_UNDERLINE const* underline,
     IUnknown* clientDrawingEffect) {
        Shape current;
        current.append(Path {PathOp::MoveTo, {Point {0, 0 + underline->offset}}});
        current.append(Path {
            PathOp::LinesTo,
            {
                Point {0 + underline->width, 0 + underline->offset},
                Point {0 + underline->width, 0 + underline->offset + underline->thickness},
                Point {0, 0 + underline->offset + underline->thickness},
            }});

        auto mat = D2D1::Matrix3x2F::Rotation(static_cast<float>(orientationAngle * 90));
        mat.dx = baselineOriginX;
        mat.dy = baselineOriginY;
        auto trans = current.transform(*reinterpret_cast<Transform*>(&mat));

        shape.extend(trans);

        return S_OK;
    }

    IFACEMETHOD(DrawUnderline)
    (void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_UNDERLINE const* underline, IUnknown* clientDrawingEffect) {
        return DrawUnderline(clientDrawingContext, baselineOriginX, baselineOriginY, DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES, underline, clientDrawingEffect);
    }

    IFACEMETHOD(IsPixelSnappingDisabled)(void* clientDrawingContext, BOOL* isDisabled) {
        *isDisabled = TRUE;
        return S_OK;
    }

    IFACEMETHOD(GetCurrentTransform)(void* clientDrawingContext, DWRITE_MATRIX* transform) { return E_NOTIMPL; }

    IFACEMETHOD(GetPixelsPerDip)(void* clientDrawingContext, FLOAT* pixelsPerDip) { return E_NOTIMPL; }

    IFACEMETHOD_(unsigned long, AddRef)() { return ++refcount; }

    IFACEMETHOD_(unsigned long, Release)() {
        auto new_count = --refcount;
        if (new_count == 0)
            delete this;
        return new_count;
    }

    IFACEMETHOD(QueryInterface)(IID const& riid, void** ppvObject) {
        if (__uuidof(IDWriteTextRenderer1) == riid || __uuidof(IDWriteTextRenderer) == riid || __uuidof(IDWritePixelSnapping) == riid ||
            __uuidof(IUnknown) == riid)
            *ppvObject = this;
        else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
};

struct TextStyle {
    // both format and layout
    std::optional<std::string> fn;
    std::optional<float> fs;
    std::optional<std::string> locale;
    std::vector<std::pair<uint32_t, float>> axis_values;

    // only layout
    std::optional<bool> u;
    std::optional<bool> s;
    std::optional<float> fsp;
    std::optional<bool> kern;
    std::vector<std::pair<uint32_t, uint32_t>> feat;

    // range
    std::pair<uint32_t, uint32_t> range;

    // global or only format
    std::optional<std::pair<float, float>> lbox;
    std::optional<std::pair<int, int>> dir;
    std::optional<int> wrap;
    std::optional<int> an;
    std::optional<int> alignment;
    std::optional<float> lsp;
    bool dip = false;
};

struct TextMetrics {
    float left;
    float top;
    float width;
    float height;
    unsigned line_count;
};

class TextLayout {
    wil::com_ptr<IDWriteTextLayout4> layout;

    float fs_ratio = 1;

    [[nodiscard]] float pt2dip(float pt) const { return pt / fs_ratio; }

  public:
    TextLayout(std::string_view text, TextStyle base_style) {
        auto factory = get_dwrite_factory();

        wil::com_ptr<IDWriteTextFormat3> format;
        static_assert(sizeof(DWRITE_FONT_AXIS_VALUE) == sizeof(decltype(base_style.axis_values)::value_type), "incompatible memory layout");
        THROW_IF_FAILED(factory->CreateTextFormat(
            u2w(base_style.fn.value_or("Arial")).c_str(),
            nullptr,
            reinterpret_cast<DWRITE_FONT_AXIS_VALUE const*>(base_style.axis_values.data()),
            static_cast<UINT32>(base_style.axis_values.size()),
            pt2dip(base_style.fs.value_or(48)),
            u2w(base_style.locale.value_or("en_US")).c_str(),
            format.put()));

        if (base_style.dir) {
            auto d = base_style.dir.value();
            format->SetReadingDirection(static_cast<DWRITE_READING_DIRECTION>(d.first));
            format->SetFlowDirection(static_cast<DWRITE_FLOW_DIRECTION>(d.second));
        }
        if (base_style.wrap) {
            format->SetWordWrapping(static_cast<DWRITE_WORD_WRAPPING>(base_style.wrap.value()));
        }
        if (base_style.an) {
            auto an = base_style.an.value();
            DWRITE_TEXT_ALIGNMENT alignment;
            switch (an % 3) {
                case 1: alignment = DWRITE_TEXT_ALIGNMENT_LEADING; break;
                case 2: alignment = DWRITE_TEXT_ALIGNMENT_CENTER; break;
                case 0: alignment = DWRITE_TEXT_ALIGNMENT_TRAILING; break;
            }
            format->SetTextAlignment(alignment);
        }
        if (base_style.alignment) {
            format->SetTextAlignment(static_cast<DWRITE_TEXT_ALIGNMENT>(base_style.alignment.value()));
        }
        if (base_style.lsp) {
            DWRITE_LINE_SPACING ls = {
                DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, base_style.lsp.value(), 1.f, 0.f, DWRITE_FONT_LINE_GAP_USAGE_DISABLED,
            };
            format->SetLineSpacing(&ls);
        }

        auto format0 = format.query<IDWriteTextFormat>();
        wil::com_ptr<IDWriteTextLayout> layout0;
        auto wtext = u2w(text);
        auto layout_box_size = base_style.lbox.value_or(std::make_pair(1920.f, 1080.f));
        THROW_IF_FAILED(factory->CreateTextLayout(
            wtext.c_str(), static_cast<UINT32>(wtext.size()), format0.get(), layout_box_size.first, layout_box_size.second, layout0.put()));
        layout = layout0.query<IDWriteTextLayout4>();

        if (!base_style.dip) {
            wil::com_ptr<FontFaceRetriever> renderer {new FontFaceRetriever};
            THROW_IF_FAILED(layout->Draw(nullptr, renderer.get(), 0, 0));
            if (!renderer->fontfaces.empty()) {
                DWRITE_FONT_METRICS fm;
                renderer->fontfaces.front()->GetMetrics(&fm);
                fs_ratio = static_cast<float>(fm.ascent + fm.descent) / static_cast<float>(fm.designUnitsPerEm);
            }
        }

        base_style.range = std::make_pair(0, static_cast<uint32_t>(wtext.size()));
        base_style.fn.reset();
        base_style.locale.reset();
        base_style.axis_values.clear();
        set_style(base_style);
    }

    void set_style(const TextStyle& override) {
        DWRITE_TEXT_RANGE range = {override.range.first, override.range.second};
        if (override.fn)
            layout->SetFontFamilyName(u2w(override.fn.value()).c_str(), range);
        if (override.fs)
            layout->SetFontSize(pt2dip(override.fs.value()), range);
        if (override.locale)
            layout->SetLocaleName(u2w(override.locale.value()).c_str(), range);
        if (!override.axis_values.empty())
            layout->SetFontAxisValues(
                reinterpret_cast<DWRITE_FONT_AXIS_VALUE const*>(override.axis_values.data()), static_cast<UINT32>(override.axis_values.size()), range);
        if (override.u)
            layout->SetUnderline(override.u.value(), range);
        if (override.s)
            layout->SetStrikethrough(override.s.value(), range);
        if (override.fsp)
            layout->SetCharacterSpacing(0, override.fsp.value(), 0, range);
        if (override.kern)
            layout->SetPairKerning(override.kern.value(), range);
        if (!override.feat.empty()) {
            wil::com_ptr<IDWriteTypography> typo;
            THROW_IF_FAILED(get_dwrite_factory()->CreateTypography(typo.put()));
            for (auto [tag, value] : override.feat) {
                THROW_IF_FAILED(typo->AddFontFeature(DWRITE_FONT_FEATURE {static_cast<DWRITE_FONT_FEATURE_TAG>(tag), value}));
            }
            layout->SetTypography(typo.get(), range);
        }
    }

    [[nodiscard]] Shape draw() const {
        Shape shape;
        wil::com_ptr<IDWriteTextRenderer> renderer {new TextRenderer {shape}};
        THROW_IF_FAILED(layout->Draw(nullptr, renderer.get(), 0, 0));
        return shape;
    }

    [[nodiscard]] TextMetrics metrics() const {
        DWRITE_TEXT_METRICS1 metrics;
        THROW_IF_FAILED(layout->GetMetrics(&metrics));
        return {metrics.left, metrics.top, metrics.widthIncludingTrailingWhitespace, metrics.heightIncludingTrailingWhitespace, metrics.lineCount};
    }
};

class Hex {
    uint32_t v;
    int width;

  public:
    Hex(uint32_t v, int width): v(v), width(width) {}

    friend StringBuilder& operator<<(StringBuilder&, Hex);

    STR_ADP_IMPLICIT_TOSTRING
};

StringBuilder& operator<<(StringBuilder& os, Hex hx) {
    auto [v, width] = hx;
    const char digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    char buf[8];
    for (std::size_t i = 0; i < width; ++i) {
        buf[i] = digits[v % 16];
        v /= 16;
    }
    os.str().append(std::reverse_iterator<const char*>(buf + width), std::reverse_iterator<const char*>(buf));
    return os;
}

class Color {
    enum class Type {
        Color,
        CurrentColor,
        None,
    };
    Type type;

    explicit Color(Type type, unsigned char r = 0, unsigned char g = 0, unsigned char b = 0, unsigned char a = 0) : type(type), r(r), g(g), b(b), a(a) {}

  public:
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;

    Color(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : type(Type::Color), r(r), g(g), b(b), a(a) {}

    [[nodiscard]] static Color current_color(unsigned char a = 0) { return Color {Type::CurrentColor, 0, 0, 0, a}; }

    [[nodiscard]] static Color none() { return Color {Type::None}; }

    [[nodiscard]] bool is_current_color() const { return type == Type::CurrentColor; }

    [[nodiscard]] bool is_none() const { return type == Type::None; }

    explicit operator bool() const { return !is_none(); }

    [[nodiscard]] Hex to_hex_bgr() const { return Hex(reinterpret_cast<const uint32_t&>(r), 6); }

    [[nodiscard]] Hex to_hex_alpha() const { return Hex(a, 2); }

    [[nodiscard]] static Color from_hex(std::string_view s) {
        if (!s.empty() && s.front() == '&')
            s = s.substr(1);
        if (!s.empty() && s.front() == 'H')
            s = s.substr(1);
        if (!s.empty() && s.back() == '&')
            s = s.substr(0, s.size() - 1);
        Color color {0, 0, 0, 0};
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), reinterpret_cast<uint32_t&>(color.r), 16);
        if (ec != std::errc())
            throw std::invalid_argument("invalid color hex");
        return color;
    }
};

struct Composition;
struct Line;

struct Context {
    Box anchor;
    Transform transform;
    Color fill;
    Color stroke;
    Shape::StrokeStyle stroke_style;
    std::shared_ptr<const Composition> mask;
};

struct Composition {
    Shape shape;
    Context context;
    std::shared_ptr<const Composition> parent;

    Composition(Shape shape, Context context, std::shared_ptr<const Composition> parent)
        : shape(std::move(shape)), context(std::move(context)), parent(std::move(parent)) {}

    [[nodiscard]] Shape to_shape(float tolerance) const;
    [[nodiscard]] std::vector<Line> to_lines(Point pos, int an, float tolerance) const;

  private:
    mutable std::optional<Shape> cache;

    void render_post_process(Shape rendered, Shape clip, std::vector<Line>& lines, Color color, Point pos, float tolerance) const;
};

template <typename T> struct cow_ptr {
    typedef T* pointer;
    typedef std::unique_ptr<T> owned_pointer;
    typedef T element_type;

  private:
    std::variant<pointer, owned_pointer> p;

  public:
    explicit cow_ptr(pointer p) noexcept : p(p) {}
    explicit cow_ptr(owned_pointer p) noexcept : p(std::move(p)) {}

    [[nodiscard]] bool is_borrowed() const noexcept { return std::holds_alternative<pointer>(p); }

    [[nodiscard]] bool is_owned() const noexcept { return !is_borrowed(); }

    [[nodiscard]] pointer get() const noexcept {
        if (is_borrowed())
            return std::get<pointer>(p);
        else
            return std::get<owned_pointer>(p).get();
    }

    explicit operator bool() const noexcept { return get(); }

    typename std::add_lvalue_reference<T>::type operator*() const noexcept(noexcept(*std::declval<pointer>())) { return *get(); }

    pointer operator->() const noexcept { return get(); }

    [[nodiscard]] T into_owned() noexcept {
        if (is_owned()) {
            owned_pointer& pp = std::get<owned_pointer>(p);
            T v {std::move(*pp)};
            pp = nullptr;
            return v;
        } else {
            pointer& pp = std::get<pointer>(p);
            T v {*pp};
            pp = nullptr;
            return v;
        }
    }
};

template <class T, class... Args> cow_ptr<T> make_cow(Args&&... args) {
    return cow_ptr<T>(std::make_unique<T>(std::forward<Args>(args)...));
}

struct Line {
    cow_ptr<Shape> draw;
    cow_ptr<Shape> clip;
    Color color;
    Point pos;

    [[nodiscard]] std::string tostring(int decimal_places) const;
};

Shape Composition::to_shape(float tolerance) const {
    if (cache)
        return cache.value();
    auto canvas = parent ? parent->to_shape(tolerance) : Shape {};
    Shape clip;
    if (context.mask) {
        clip = context.mask->to_shape(tolerance);
        if (clip.empty())
            return canvas;
    }
    if (context.fill) {
        auto rendered = shape.filter_open().transform(context.transform);
        if (!clip.empty())
            rendered = rendered.combine(clip, Shape::CombineMode::INTERSECT, tolerance);
        if (!canvas.empty())
            canvas = canvas.combine(rendered, Shape::CombineMode::UNION, tolerance);
        else
            canvas = std::move(rendered);
    }
    if (context.stroke && context.stroke_style.width > 0) {
        auto rendered = shape.widen1(context.stroke_style, tolerance, context.transform);
        if (!clip.empty())
            rendered = rendered.combine(clip, Shape::CombineMode::INTERSECT, tolerance);
        if (!canvas.empty())
            canvas = canvas.combine(rendered, Shape::CombineMode::UNION, tolerance);
        else
            canvas = std::move(rendered);
    }
    cache = std::move(canvas);
    return cache.value();
}

std::vector<Line> Composition::to_lines(Point pos, int an, float tolerance) const {
    auto lines = parent ? parent->to_lines(pos, an, tolerance) : std::vector<Line> {};
    Shape clip;
    if (context.mask) {
        clip = context.mask->to_shape(tolerance);
        if (clip.empty())
            return lines;
    }
    auto anchor = context.anchor;
    if (!clip.empty()) {
        Transform trans {1, 0, 0, 1, pos.x, pos.y};
        switch ((an - 1) % 3) {
            case 1: trans.dx -= anchor.width / 2; break;
            case 2: trans.dx -= anchor.width; break;
        }
        switch ((an - 1) / 3) {
            case 0: trans.dy -= anchor.height; break;
            case 1: trans.dy -= anchor.height / 2; break;
        }
        clip = clip.transform(trans);
    }
    if (context.fill) {
        auto rendered = shape.filter_open().transform(context.transform);
        render_post_process(std::move(rendered), clip, lines, context.fill, pos, tolerance);
    }
    if (context.stroke && context.stroke_style.width > 0) {
        auto rendered = shape.widen1(context.stroke_style, tolerance, context.transform);
        render_post_process(std::move(rendered), clip, lines, context.stroke, pos, tolerance);
    }
    return lines;
}

void Composition::render_post_process(Shape rendered, Shape clip, std::vector<Line>& lines, Color color, Point pos, float tolerance) const {
    if (rendered.empty())
        return;
    auto bounds = rendered.minmax().value();
    auto anchor = context.anchor;
    if (bounds.left < anchor.left || bounds.top < anchor.top || bounds.left + bounds.width > anchor.left + anchor.width ||
        bounds.top + bounds.height > anchor.top + anchor.height) {
        Shape anchor_mask {anchor.to_mask()};
        rendered = rendered.combine(anchor_mask, Shape::CombineMode::INTERSECT, tolerance);
    }
    rendered = rendered.anchor(anchor);
    lines.emplace_back(Line {make_cow<Shape>(std::move(rendered)), make_cow<Shape>(std::move(clip)), color, pos});
}

std::string Line::tostring(int decimal_places) const {
    StringBuilder os;
    os << "{\\pos(" << FixedF(pos.x, decimal_places) << ',' << FixedF(pos.y, decimal_places) << ')';
    if (!clip->empty())
        os << "\\clip(" << clip->tostring(decimal_places) << ')';
    if (!color.is_current_color())
        os << "\\c&H" << color.to_hex_bgr() << '&';
    if (color.a != 0)
        os << "\\1a&H" << color.to_hex_alpha() << '&';
    os << "\\p1}" << draw->tostring(decimal_places);
    return os.into_str();
}

class Svg {
    [[nodiscard]] static std::wstring get_tag_name(ID2D1SvgElement* elem) {
        wchar_t tag[32];
        if (elem->GetTagName(tag, 32) != S_OK)
            return {};
        return tag;
    }

    [[nodiscard]] static Color paint2color(ID2D1SvgPaint* paint) {
        D2D1_COLOR_F c;
        switch (paint->GetPaintType()) {
            case D2D1_SVG_PAINT_TYPE_CURRENT_COLOR: return Color::current_color();
            case D2D1_SVG_PAINT_TYPE_COLOR:
                paint->GetColor(&c);
                // D2D1_COLOR_F is floating-point, but not linear (-o-!)
                return Color {
                    static_cast<unsigned char>(c.r * 255.f),
                    static_cast<unsigned char>(c.g * 255.f),
                    static_cast<unsigned char>(c.b * 255.f),
                    static_cast<unsigned char>((1 - c.a) * 255.f),
                };
            default: return Color::none();
        }
    }

    struct ViewBoxSize {
        float width;
        float height;
        [[nodiscard]] float diagonal() const { return sqrtf(width * width + height * height); }
    };

    [[nodiscard]] static float parse_length(D2D1_SVG_LENGTH length, float total) {
        if (length.units == D2D1_SVG_LENGTH_UNITS_PERCENTAGE)
            return length.value * total / 100;
        else
            return length.value;
    }

    static void recur_svg(ID2D1SvgElement* elem, const Context& parent_ctx, ViewBoxSize vb, std::shared_ptr<const Composition>& comp) {
        Shape shape;
        auto tag = get_tag_name(elem);

        if (tag == L"g")
            ;
        else if (tag == L"path") {
            wil::com_ptr<ID2D1SvgPathData> path_data;
            if (elem->GetAttributeValue(L"d", path_data.put()) != S_OK)
                return;
            wil::com_ptr<ID2D1PathGeometry1> geo;
            THROW_IF_FAILED(path_data->CreatePathGeometry(D2D1_FILL_MODE_WINDING, geo.put()));
            shape.load_from_geometry(geo.get());
        } else if (tag == L"circle" || tag == L"ellipse") {
            float cx = 0, cy = 0, rx, ry;
            D2D1_SVG_LENGTH length;
            if (elem->GetAttributeValue(L"cx", &length) == S_OK)
                cx = parse_length(length, vb.width);
            if (elem->GetAttributeValue(L"cy", &length) == S_OK)
                cy = parse_length(length, vb.height);
            if (tag == L"circle") {
                if (elem->GetAttributeValue(L"r", &length) != S_OK)
                    return;
                rx = ry = parse_length(length, vb.diagonal());
            } else {
                if (elem->GetAttributeValue(L"rx", &length) != S_OK)
                    return;
                rx = parse_length(length, vb.width);
                if (elem->GetAttributeValue(L"ry", &length) != S_OK)
                    return;
                ry = parse_length(length, vb.height);
            }
            shape.append(Path {PathOp::MoveTo, {Point {1, 0}}});
            constexpr float magic = 0.552284749831f;
            shape.append(Path {
                PathOp::BeziersTo,
                {
                    Point {1, -magic},
                    Point {magic, -1},
                    Point {0, -1},
                    Point {-magic, -1},
                    Point {-1, -magic},
                    Point {-1, 0},
                    Point {-1, magic},
                    Point {-magic, 1},
                    Point {0, 1},
                    Point {magic, 1},
                    Point {1, magic},
                    Point {1, 0},
                }});
            shape = shape.transform(Transform {rx, 0, 0, -ry, cx, cy});
        } else if (tag == L"line") {
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            D2D1_SVG_LENGTH length;
            if (elem->GetAttributeValue(L"x1", &length) == S_OK)
                x1 = parse_length(length, vb.width);
            if (elem->GetAttributeValue(L"x2", &length) == S_OK)
                x2 = parse_length(length, vb.width);
            if (elem->GetAttributeValue(L"y1", &length) == S_OK)
                y1 = parse_length(length, vb.height);
            if (elem->GetAttributeValue(L"y2", &length) == S_OK)
                y2 = parse_length(length, vb.height);
            shape.append(Path {PathOp::MoveTo, {Point {x1, y1}}});
            shape.append(Path {PathOp::LinesTo, {Point {x2, y2}}});
            shape.append(Path {PathOp::End, {}});
        } else if (tag == L"polyline" || tag == L"polygon") {
            wil::com_ptr<ID2D1SvgPointCollection> point_collection;
            if (elem->GetAttributeValue(L"points", point_collection.put()) != S_OK)
                return;
            auto count = point_collection->GetPointsCount();
            if (count < 2)
                return;
            Point start_point;
            point_collection->GetPoints(reinterpret_cast<D2D1_POINT_2F*>(&start_point), 1, 0);
            shape.append(Path {PathOp::MoveTo, {start_point}});
            Path::PointVec following(count - 1);
            point_collection->GetPoints(reinterpret_cast<D2D1_POINT_2F*>(following.data()), count - 1, 1);
            shape.append(Path {PathOp::LinesTo, std::move(following)});
            if (tag == L"polyline")
                shape.append(Path {PathOp::End, {}});
        } else if (tag == L"rect") {
            float x = 0, y = 0, w, h, rx = 0, ry = 0;
            bool no_rx = false, no_ry = false;
            D2D1_SVG_LENGTH length;
            if (elem->GetAttributeValue(L"x", &length) == S_OK)
                x = parse_length(length, vb.width);
            if (elem->GetAttributeValue(L"y", &length) == S_OK)
                y = parse_length(length, vb.height);
            if (elem->GetAttributeValue(L"width", &length) != S_OK)
                return;
            w = parse_length(length, vb.width);
            if (elem->GetAttributeValue(L"height", &length) != S_OK)
                return;
            h = parse_length(length, vb.height);
            if (elem->GetAttributeValue(L"rx", &length) == S_OK)
                rx = parse_length(length, vb.width);
            else
                no_rx = true;
            if (elem->GetAttributeValue(L"ry", &length) == S_OK)
                ry = parse_length(length, vb.height);
            else
                no_ry = true;
            if (no_rx && !no_ry)
                rx = ry;
            if (!no_rx && no_ry)
                ry = rx;
            D2D1_ROUNDED_RECT rr {{x, y, x + w, y + h}, rx, ry};
            wil::com_ptr<ID2D1RoundedRectangleGeometry> rr_geo;
            THROW_IF_FAILED(get_d2d1_factory()->CreateRoundedRectangleGeometry(rr, rr_geo.put()));
            shape.load_from_geometry(rr_geo.get());
        } else
            return;

        auto current_transform = Transform::identity();
        void(elem->GetAttributeValue(L"transform", reinterpret_cast<D2D1_MATRIX_3X2_F*>(&current_transform)));
        auto transform = current_transform * parent_ctx.transform;

        auto fill = parent_ctx.fill;
        wil::com_ptr<ID2D1SvgPaint> paint;
        if (elem->GetAttributeValue(L"fill", paint.put()) == S_OK)
            fill = paint2color(paint.get());

        float opacity;
        // XXX: percentage not supported
        if (elem->GetAttributeValue(L"fill-opacity", &opacity) == S_OK)
            fill.a = static_cast<unsigned char>((1 - opacity) * 255);

        auto stroke = parent_ctx.stroke;
        if (elem->GetAttributeValue(L"stroke", paint.put()) == S_OK)
            stroke = paint2color(paint.get());

        if (elem->GetAttributeValue(L"stroke-opacity", &opacity) == S_OK)
            stroke.a = static_cast<unsigned char>((1 - opacity) * 255);

        auto stroke_style = parent_ctx.stroke_style;
        D2D1_SVG_LENGTH length;
        if (elem->GetAttributeValue(L"stroke-width", &length) == S_OK)
            stroke_style.width = parse_length(length, vb.diagonal());

        D2D1_SVG_LINE_CAP cap;
        if (elem->GetAttributeValue(L"stroke-linecap", &cap) == S_OK)
            stroke_style.start_cap = stroke_style.dash_cap = stroke_style.end_cap = static_cast<Shape::StrokeStyle::LineCap>(cap);

        D2D1_SVG_LINE_JOIN join;
        if (elem->GetAttributeValue(L"stroke-linejoin", &join) == S_OK)
            stroke_style.line_join = static_cast<Shape::StrokeStyle::LineJoin>(join);

        float miter_limit;
        if (elem->GetAttributeValue(L"stroke-miterlimit", &miter_limit) == S_OK)
            stroke_style.miter_limit = miter_limit;

        wil::com_ptr<ID2D1SvgStrokeDashArray> dash_array;
        if (elem->GetAttributeValue(L"stroke-dasharray", dash_array.put()) == S_OK) {
            // XXX: percentage not supported
            auto count = dash_array->GetDashesCount();
            if (count == 0)
                stroke_style.dash_style = Shape::StrokeStyle::DashStyle::SOLID;
            else {
                std::vector<float> dash(count);
                THROW_IF_FAILED(dash_array->GetDashes(dash.data(), count));
                stroke_style.dash_style = Shape::StrokeStyle::DashStyle::CUSTOM;
                stroke_style.dash_pattern = std::move(dash);
            }
        }

        float dash_offset;
        if (elem->GetAttributeValue(L"stroke-dashoffset", &dash_offset) == S_OK)
            // XXX: percentage not supported
            stroke_style.dash_offset = dash_offset;

        D2D1_SVG_DISPLAY display;
        if (elem->GetAttributeValue(L"display", &display) == S_OK)
            if (display == D2D1_SVG_DISPLAY_NONE)
                return;

        D2D1_SVG_VISIBILITY visibility;
        if (elem->GetAttributeValue(L"visibility", &visibility) == S_OK)
            if (visibility == D2D1_SVG_VISIBILITY_HIDDEN)
                return;

        auto anchor = parent_ctx.anchor;
        auto mask = parent_ctx.mask;

        Context cur_ctx {anchor, transform, fill, stroke, std::move(stroke_style), std::move(mask)};

        if (!shape.empty())
            comp = std::make_shared<Composition>(shape, cur_ctx, std::move(comp));

        if (tag == L"g") {
            wil::com_ptr<ID2D1SvgElement> child;
            elem->GetFirstChild(child.put());
            while (child) {
                recur_svg(child.get(), cur_ctx, vb, comp);
                wil::com_ptr<ID2D1SvgElement> next;
                THROW_IF_FAILED(elem->GetNextChild(child.get(), next.put()));
                child = std::move(next);
            }
        }
    }

  public:
    [[nodiscard]] static std::shared_ptr<const Composition> load(std::string_view s) {
        wil::com_ptr<IStream> stream;
        if (!s.empty() && s[0] == '<')
            stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(s.data()), static_cast<UINT>(s.size()));
        else
            THROW_IF_FAILED(SHCreateStreamOnFileEx(u2w(s).c_str(), STGM_READ | STGM_SHARE_DENY_WRITE | STGM_FAILIFTHERE, 0, 0, nullptr, stream.put()));
        wil::com_ptr<ID2D1SvgDocument> doc;
        THROW_IF_FAILED(get_d2d1_device_context()->CreateSvgDocument(stream.get(), {350, 150}, doc.put()));
        wil::com_ptr<ID2D1SvgElement> root;
        doc->GetRoot(root.put());
        if (get_tag_name(root.get()) != L"svg")
            throw std::runtime_error("invalid SVG document");
        D2D1_SVG_VIEWBOX view_box;
        bool no_view_box = false;
        if (root->GetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &view_box, sizeof(view_box)) != S_OK) {
            view_box = {0, 0, 300, 150};
            no_view_box = true;
        }
        D2D1_SIZE_F view_port;
        D2D1_SVG_LENGTH length;
        bool no_width = false, no_height = false;
        if (root->GetAttributeValue(L"width", &length) == S_OK)
            view_port.width = parse_length(length, view_box.width);
        else
            no_width = true;
        if (root->GetAttributeValue(L"height", &length) == S_OK)
            view_port.height = parse_length(length, view_box.height);
        else
            no_height = true;
        if (no_width && no_height)
            view_port = {view_box.width, view_box.height};
        else if (no_width)
            view_port.width = view_port.height / view_box.height * view_box.width;
        else if (no_height)
            view_port.height = view_port.width / view_box.width * view_box.height;
        if (!no_width && !no_height && no_view_box)
            view_box = {0, 0, view_port.width, view_port.height};
        D2D1_SVG_PRESERVE_ASPECT_RATIO par;
        if (root->GetAttributeValue(L"preserveAspectRatio", D2D1_SVG_ATTRIBUTE_POD_TYPE_PRESERVE_ASPECT_RATIO, &par, sizeof(par)) != S_OK)
            par = {false, D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID, D2D1_SVG_ASPECT_SCALING_MEET};
        float scale_x = view_port.width / view_box.width;
        float scale_y = view_port.height / view_box.height;
        float scale_u = par.meetOrSlice == D2D1_SVG_ASPECT_SCALING_MEET ? std::min(scale_x, scale_y) : std::max(scale_x, scale_y);
        Transform root_transform;
        if (par.align == D2D1_SVG_ASPECT_ALIGN_NONE)
            root_transform = {scale_x, 0, 0, scale_y, 0, 0};
        else
            root_transform = {scale_u, 0, 0, scale_u, 0, 0};
        switch (par.align) {
            case D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MID:
            case D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID:
            case D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MID: root_transform.dy = (view_port.height - view_box.height * root_transform.m22) / 2; break;
            case D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MAX:
            case D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MAX:
            case D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MAX: root_transform.dy = view_port.height - view_box.height * root_transform.m22; break;
            default:;
        }
        switch (par.align) {
            case D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MIN:
            case D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID:
            case D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MAX: root_transform.dx = (view_port.width - view_box.width * root_transform.m11) / 2; break;
            case D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MIN:
            case D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MID:
            case D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MAX: root_transform.dx = view_port.width - view_box.width * root_transform.m11; break;
            default:;
        }
        root_transform.dx -= view_box.x * root_transform.m11;
        root_transform.dy -= view_box.y * root_transform.m22;
        Shape view_port_mask {Box {0, 0, view_port.width, view_port.height}.to_mask()};
        Context root_context {
            {0, 0, view_port.width, view_port.height},
            root_transform,
            Color::current_color(),
            Color::none(),
            {},
            std::make_shared<Composition>(view_port_mask, Context {{}, Transform::identity(), {0, 0, 0, 0}, Color::none(), {}, {}}, nullptr)};
        std::shared_ptr<const Composition> comp;
        wil::com_ptr<ID2D1SvgElement> child;
        root->GetFirstChild(child.put());
        while (child) {
            recur_svg(child.get(), root_context, {view_box.width, view_box.height}, comp);
            wil::com_ptr<ID2D1SvgElement> next;
            THROW_IF_FAILED(root->GetNextChild(child.get(), next.put()));
            child = std::move(next);
        }
        return comp;
    }
};

#define MAKE_TNAME(cls) ("mskfunc." #cls)

[[noreturn]] void lerror(lua_State* L) {
    lua_error(L);
    __assume(false);
}

[[noreturn]] void llerror(lua_State* L, const char* msg) {
    luaL_error(L, msg);
    __assume(false);
}

std::optional<float> table_getf(lua_State* L, const char* name) {
    lua_getfield(L, -1, name);
    int isnum;
    auto v = static_cast<float>(lua_tonumberx(L, -1, &isnum));
    lua_pop(L, 1);
    if (isnum)
        return v;
    else
        return std::nullopt;
}

std::optional<std::string> table_gets(lua_State* L, const char* name) {
    lua_getfield(L, -1, name);
    auto p = lua_tostring(L, -1);
    std::string v;
    if (p)
        v = p;
    lua_pop(L, 1);
    if (p)
        return v;
    else
        return std::nullopt;
}

std::optional<bool> table_getb(lua_State* L, const char* name) {
    lua_getfield(L, -1, name);
    auto isbool = lua_isboolean(L, -1);
    auto v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (isbool)
        return v;
    else
        return std::nullopt;
}

template <typename T> int generic_dtor(lua_State* L) {
    auto ud = static_cast<T*>(lua_touserdata(L, 1));
    ud->~T();
    return 0;
}

int generic_getmethod(lua_State* L) {
    auto index = luaL_checkstring(L, 2);
    for (auto method = static_cast<const luaL_Reg*>(lua_topointer(L, lua_upvalueindex(1))); method->name; ++method) {
        if (strcmp(method->name, index) == 0) {
            lua_pushcfunction(L, method->func);
            return 1;
        }
    }
    return 0;
}

static struct {
    int decimal_places = 0;
    float flattening_tolerance = 0;
} mskfunc_ctx;

int get_contextfield(lua_State* L) {
    auto name = lua_tostring(L, 2);

    if (strcmp(name, "decimal_places") == 0)
        lua_pushinteger(L, mskfunc_ctx.decimal_places);
    else if (strcmp(name, "flattening_tolerance") == 0)
        lua_pushnumber(L, mskfunc_ctx.flattening_tolerance);
    else
        return 0;

    return 1;
}

int set_contextfield(lua_State* L) {
    auto name = lua_tostring(L, 2);

    if (strcmp(name, "decimal_places") == 0)
        mskfunc_ctx.decimal_places = static_cast<int>(lua_tointeger(L, 3));
    else if (strcmp(name, "flattening_tolerance") == 0)
        mskfunc_ctx.flattening_tolerance = static_cast<float>(lua_tonumber(L, 3));
    else
        llerror(L, "no such field");

    return 0;
}

[[nodiscard]] float get_flattening_tolerance() {
    if (auto tolerance = mskfunc_ctx.flattening_tolerance; tolerance == 0)
        return D2D1_DEFAULT_FLATTENING_TOLERANCE;
    else
        return tolerance;
}

luaL_Reg context_libmeta[] = {{"__index", get_contextfield}, {"__newindex", set_contextfield}, {nullptr, nullptr}};

void create_context_lib(lua_State* L) {
    lua_newtable(L);
    luaL_newlib(L, context_libmeta);
    lua_setmetatable(L, -2);
}

[[nodiscard]] Box pull_box(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    lua_getfield(L, idx, "left");
    lua_getfield(L, idx, "top");
    lua_getfield(L, idx, "width");
    lua_getfield(L, idx, "height");
    Box box {
        static_cast<float>(lua_tonumber(L, -4)),
        static_cast<float>(lua_tonumber(L, -3)),
        static_cast<float>(lua_tonumber(L, -2)),
        static_cast<float>(lua_tonumber(L, -1)),
    };
    lua_pop(L, 4);
    return box;
}

void new_box(lua_State* L, Box box) {
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, box.left);
    lua_setfield(L, -2, "left");
    lua_pushnumber(L, box.top);
    lua_setfield(L, -2, "top");
    lua_pushnumber(L, box.width);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, box.height);
    lua_setfield(L, -2, "height");
}

[[nodiscard]] Shape* pull_shape(lua_State* L, int idx) {
    return static_cast<Shape*>(luaL_checkudata(L, idx, MAKE_TNAME(Shape)));
}

template <typename... Args> Shape* new_shape(lua_State* L, Args&&... args) {
    auto shape = static_cast<Shape*>(lua_newuserdata(L, sizeof(Shape)));
    new (shape) Shape(std::forward<Args>(args)...);
    luaL_setmetatable(L, MAKE_TNAME(Shape));
    return shape;
}

void push_string(lua_State* L, std::string_view s) {
    lua_pushlstring(L, s.data(), s.size());
}

void push_string(lua_State* L, const std::string& s) {
    push_string(L, std::string_view {s});
}

int shape_tostring(lua_State* L) {
    push_string(L, pull_shape(L, 1)->tostring(mskfunc_ctx.decimal_places));
    return 1;
}

int shape_len(lua_State* L) {
    auto shape = pull_shape(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(shape->size()));
    return 1;
}

int shape_concat(lua_State* L) {
    auto shape1 = pull_shape(L, 1);
    auto shape2 = pull_shape(L, 2);

    auto shape3 = new_shape(L, *shape1);
    shape3->extend(*shape2);

    return 1;
}

luaL_Reg shape_metatable[] = {{"__gc", generic_dtor<Shape>}, {"__index", generic_getmethod}, {"__tostring", shape_tostring},
                              {"__len", shape_len},          {"__concat", shape_concat},     {nullptr, nullptr}};

[[noreturn]] void out_of_range(lua_State* L) {
    llerror(L, "index out of range");
}

[[nodiscard]] Transform pull_transform(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    Transform m;
    for (int i = 1; i <= 6; ++i) {
        lua_pushnumber(L, i);
        lua_gettable(L, idx);
        reinterpret_cast<float*>(&m)[i - 1] = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    return m;
}

void new_transform(lua_State* L, const Transform& m) {
    lua_getfield(L, LUA_REGISTRYINDEX, "mskfunc.transform");
    lua_getfield(L, -1, "new");
    lua_remove(L, -2);
    for (int i = 0; i < 6; ++i)
        lua_pushnumber(L, reinterpret_cast<const float*>(&m)[i]);
    lua_call(L, 6, 1);
}

int shape_transform(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto m = pull_transform(L, 2);
    new_shape(L, shape->transform(m));
    return 1;
}

int shape_subset(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto begin = luaL_optinteger(L, 2, 1);
    auto end = luaL_optinteger(L, 3, begin);

    if (--begin < 0 || end + 0ull >= shape->size() || begin >= end)
        out_of_range(L);

    new_shape(L, shape->subset(begin, end));

    return 1;
}

int shape_get_path(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto idx = luaL_checkinteger(L, 2) - 1;

    if (idx < 0 || idx + 0ull >= shape->size())
        out_of_range(L);

    auto& path = shape->get(idx);
    auto op = op2char(path.op);
    lua_pushlstring(L, &op, 1);

    if (lua_isboolean(L, 3) && !lua_toboolean(L, 3))
        return 1;

    lua_createtable(L, static_cast<int>(path.points.size()), 0);
    int i = 0;
    for (auto [x, y] : path.points) {
        lua_createtable(L, 2, 0);
        lua_pushnumber(L, x);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, y);
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, ++i);
    }

    return 2;
}

int shape_set_path(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto idx = luaL_checkinteger(L, 2) - 1;
    auto ops = lua_tostring(L, 3);

    if (idx < 0 || idx + 0ull > shape->size())
        out_of_range(L);
    if (idx + 0ull == shape->size())
        shape->append(Path {PathOp::Close, {}});

    auto& path = shape->get(idx);
    Path new_path;
    if (ops)
        new_path.op = char2op(ops[0]);
    else
        new_path.op = path.op;
    if (lua_isnoneornil(L, 4))
        new_path.points = path.points;
    else {
        auto len = luaL_len(L, 4);
        for (int i = 1; i <= len; ++i) {
            lua_pushinteger(L, i);
            lua_gettable(L, 4);
            lua_rawgeti(L, -1, 1);
            lua_rawgeti(L, -2, 2);
            new_path.points.emplace_back(Point {static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1))});
            lua_pop(L, 3);
        }
    }
    shape->set(idx, std::move(new_path));

    return 0;
}

int shape_stream(lua_State* L) {
    auto shape = pull_shape(L, 1);
    lua_pushvalue(L, 2);

    auto streamed = new_shape(L);
    lua_replace(L, 1);

    for (auto& path : *shape) {
        auto op = op2char(path.op);
        lua_pushnil(L);
        lua_pushlstring(L, &op, 1);
        lua_call(L, 2, 1);
        auto skip = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
        auto rops = lua_tostring(L, -1);
        PathOp new_op;
        if (!skip && !lua_isnil(L, -1)) {
            if (!rops)
                llerror(L, "require return value of type 'string'");
            new_op = char2op(rops[0]);
        } else
            new_op = path.op;
        lua_settop(L, 2);
        lua_pushvalue(L, 2);
        if (skip)
            continue;

        Path::PointVec new_path;

        for (const auto point : path.points) {
            lua_createtable(L, 2, 0);
            lua_pushnumber(L, point.x);
            lua_rawseti(L, -2, 1);
            lua_pushnumber(L, point.y);
            lua_rawseti(L, -2, 2);
            lua_pushnil(L);
            lua_call(L, 2, 1);
            if (lua_isnil(L, -1))
                new_path.emplace_back(point);
            else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
                ;
            else {
                lua_rawgeti(L, -1, 1);
                lua_rawgeti(L, -2, 2);
                new_path.emplace_back(Point {static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1))});
            }
            lua_settop(L, 2);
            lua_pushvalue(L, 2);
        }

        streamed->append(Path {new_op, std::move(new_path)});
    }

    lua_pushvalue(L, 1);
    return 1;
}

int shape_copy(lua_State* L) {
    auto shape = pull_shape(L, 1);
    new_shape(L, *shape);
    return 1;
}

int shape_move_to(lua_State* L) {
    lua_settop(L, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(pull_shape(L, 1)->size() + 1));
    lua_pushliteral(L, "m");
    lua_createtable(L, 1, 0);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, 1);
    lua_remove(L, 2);
    return shape_set_path(L);
}

int shape_lines_to(lua_State* L) {
    lua_settop(L, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(pull_shape(L, 1)->size() + 1));
    lua_pushliteral(L, "l");
    lua_pushvalue(L, 2);
    lua_remove(L, 2);
    return shape_set_path(L);
}

int shape_beziers_to(lua_State* L) {
    lua_settop(L, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(pull_shape(L, 1)->size() + 1));
    lua_pushliteral(L, "b");
    lua_pushvalue(L, 2);
    lua_remove(L, 2);
    return shape_set_path(L);
}

int shape_close_path(lua_State* L) {
    pull_shape(L, 1)->append(Path {PathOp::Close, {}});
    return 0;
}

int shape_anchor(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto box = pull_box(L, 2);
    new_shape(L, shape->anchor(box));
    return 1;
}

int shape_bounds(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto opt_bounds = shape->bounds();
    if (!opt_bounds)
        return 0;
    new_box(L, opt_bounds.value());
    return 1;
}

int shape_minmax(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto opt_minmax = shape->minmax();
    if (!opt_minmax)
        return 0;
    new_box(L, opt_minmax.value());
    return 1;
}

int shape_combine(lua_State* L) {
    auto shape1 = pull_shape(L, 1);
    auto shape2 = pull_shape(L, 2);
    auto mode = luaL_optstring(L, 3, "+");

    Shape::CombineMode combine_mode;
    switch (mode[0]) {
        case 'u':
        case '+': combine_mode = Shape::CombineMode::UNION; break;
        case 'i':
        case '*': combine_mode = Shape::CombineMode::INTERSECT; break;
        case 'x':
        case '^': combine_mode = Shape::CombineMode::XOR; break;
        case 'e':
        case '-': combine_mode = Shape::CombineMode::EXCLUDE; break;
        default: llerror(L, "unknown combine mode");
    }

    new_shape(L, shape1->combine(*shape2, combine_mode, get_flattening_tolerance()));

    return 1;
}

int shape_compare(lua_State* L) {
    auto shape1 = pull_shape(L, 1);
    auto shape2 = pull_shape(L, 2);

    switch (shape1->compare(*shape2, get_flattening_tolerance())) {
        case Shape::Relation::DISJOINT: lua_pushliteral(L, "disjoint"); break;
        case Shape::Relation::IS_CONTAINED: lua_pushliteral(L, "is_contained"); break;
        case Shape::Relation::CONTAINS: lua_pushliteral(L, "contains"); break;
        case Shape::Relation::OVERLAP: lua_pushliteral(L, "overlap"); break;
    }

    return 1;
}

int shape_contains(lua_State* L) {
    auto shape = pull_shape(L, 1);

    lua_rawgeti(L, 2, 1);
    lua_rawgeti(L, 2, 2);
    Point point {static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1))};

    lua_pushboolean(L, shape->contains(point, get_flattening_tolerance()));

    return 1;
}

int shape_area(lua_State* L) {
    lua_pushnumber(L, pull_shape(L, 1)->compute_area(get_flattening_tolerance()));
    return 1;
}

int shape_length(lua_State* L) {
    lua_pushnumber(L, pull_shape(L, 1)->compute_length(get_flattening_tolerance()));
    return 1;
}

int shape_point_at_length(lua_State* L) {
    auto shape = pull_shape(L, 1);
    auto length = static_cast<float>(luaL_checknumber(L, 2));
    auto start_path_idx = luaL_optinteger(L, 3, 1) - 1;

    auto desc = shape->point_at_length(length, start_path_idx, get_flattening_tolerance());

    lua_createtable(L, 2, 0);
    lua_pushnumber(L, desc.point.x);
    lua_rawseti(L, -2, 1);
    lua_pushnumber(L, desc.point.y);
    lua_rawseti(L, -2, 2);
    lua_createtable(L, 2, 0);
    lua_pushnumber(L, desc.tangent.x);
    lua_rawseti(L, -2, 1);
    lua_pushnumber(L, desc.tangent.y);
    lua_rawseti(L, -2, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(desc.path_idx));
    lua_pushinteger(L, static_cast<lua_Integer>(desc.figure_idx));
    lua_pushnumber(L, desc.length_to_path);

    return 5;
}

int shape_flatten(lua_State* L) {
    new_shape(L, pull_shape(L, 1)->flatten(get_flattening_tolerance()));
    return 1;
}

int shape_outline(lua_State* L) {
    new_shape(L, pull_shape(L, 1)->outline(get_flattening_tolerance()));
    return 1;
}

std::optional<Shape::StrokeStyle::LineCap> parse_cap_style(lua_State* L) {
    auto s = lua_tostring(L, -1);
    if (!s) {
        lua_pop(L, 1);
        return std::nullopt;
    }
    Shape::StrokeStyle::LineCap cap;
    switch (s[0]) {
        case 'f': cap = Shape::StrokeStyle::LineCap::FLAT; break;
        case 's': cap = Shape::StrokeStyle::LineCap::SQUARE; break;
        case 'r': cap = Shape::StrokeStyle::LineCap::ROUND; break;
        case 't': cap = Shape::StrokeStyle::LineCap::TRIANGLE; break;
        default: llerror(L, "unknown cap style");
    }
    lua_pop(L, 1);
    return cap;
}

void unparse_cap_style(lua_State* L, Shape::StrokeStyle::LineCap cap) {
    switch (cap) {
        case Shape::StrokeStyle::LineCap::FLAT: lua_pushliteral(L, "flat"); break;
        case Shape::StrokeStyle::LineCap::SQUARE: lua_pushliteral(L, "square"); break;
        case Shape::StrokeStyle::LineCap::ROUND: lua_pushliteral(L, "round"); break;
        case Shape::StrokeStyle::LineCap::TRIANGLE: lua_pushliteral(L, "triangle"); break;
    }
}

std::optional<Shape::StrokeStyle::LineJoin> parse_line_join(lua_State* L) {
    auto s = lua_tostring(L, -1);
    if (!s) {
        lua_pop(L, 1);
        return std::nullopt;
    }
    Shape::StrokeStyle::LineJoin join;
    switch (s[0]) {
        case 'm': join = Shape::StrokeStyle::LineJoin::MITER; break;
        case 'b': join = Shape::StrokeStyle::LineJoin::BEVEL; break;
        case 'r': join = Shape::StrokeStyle::LineJoin::ROUND; break;
        case '+': join = Shape::StrokeStyle::LineJoin::MITER_OR_BEVEL; break;
        default: llerror(L, "unknown line join");
    }
    lua_pop(L, 1);
    return join;
}

void unparse_line_join(lua_State* L, Shape::StrokeStyle::LineJoin join) {
    switch (join) {
        case Shape::StrokeStyle::LineJoin::MITER: lua_pushliteral(L, "miter"); break;
        case Shape::StrokeStyle::LineJoin::BEVEL: lua_pushliteral(L, "bevel"); break;
        case Shape::StrokeStyle::LineJoin::ROUND: lua_pushliteral(L, "round"); break;
        case Shape::StrokeStyle::LineJoin::MITER_OR_BEVEL: lua_pushliteral(L, "+"); break;
    }
}

std::optional<std::pair<Shape::StrokeStyle::DashStyle, std::vector<float>>> parse_dash_style(lua_State* L) {
    auto s = lua_tostring(L, -1);
    std::vector<float> pattern;
    if (s) {
        std::string ss(s);
        lua_pop(L, 1);
        if (ss == "-")
            return std::make_pair(Shape::StrokeStyle::DashStyle::DASH, pattern);
        if (ss == ".")
            return std::make_pair(Shape::StrokeStyle::DashStyle::DOT, pattern);
        if (ss == "-.")
            return std::make_pair(Shape::StrokeStyle::DashStyle::DASH_DOT, pattern);
        if (ss == "-..")
            return std::make_pair(Shape::StrokeStyle::DashStyle::DASH_DOT_DOT, pattern);
        llerror(L, "unknown dash style");
    } else if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::nullopt;
    } else {
        auto len = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= len; ++i) {
            lua_pushinteger(L, i);
            lua_gettable(L, -2);
            pattern.push_back(static_cast<float>(lua_tonumber(L, -1)));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return std::make_pair(Shape::StrokeStyle::DashStyle::CUSTOM, pattern);
    }
}

void unparse_dash_style(lua_State* L, Shape::StrokeStyle::DashStyle dash, const std::vector<float>& pattern) {
    switch (dash) {
        case Shape::StrokeStyle::DashStyle::SOLID: lua_pushnil(L); break;
        case Shape::StrokeStyle::DashStyle::DASH: lua_pushliteral(L, "-"); break;
        case Shape::StrokeStyle::DashStyle::DOT: lua_pushliteral(L, "."); break;
        case Shape::StrokeStyle::DashStyle::DASH_DOT: lua_pushliteral(L, "-."); break;
        case Shape::StrokeStyle::DashStyle::DASH_DOT_DOT: lua_pushliteral(L, "-.."); break;
        case Shape::StrokeStyle::DashStyle::CUSTOM: {
            lua_createtable(L, static_cast<int>(pattern.size()), 0);
            int i = 0;
            for (auto v : pattern) {
                lua_pushnumber(L, v);
                lua_rawseti(L, -2, ++i);
            }
            break;
        }
    }
}

Shape::StrokeStyle create_stroke_from_table(lua_State* L) {
    Shape::StrokeStyle stroke;
    if (lua_isnumber(L, -1)) {
        stroke.width = static_cast<float>(lua_tonumber(L, -1));
        return stroke;
    }
    if (auto width = table_getf(L, "width"); width)
        stroke.width = width.value();
    lua_getfield(L, -1, "cap");
    if (auto cap = parse_cap_style(L); cap)
        stroke.start_cap = stroke.end_cap = stroke.dash_cap = cap.value();
    lua_getfield(L, -1, "start_cap");
    if (auto cap = parse_cap_style(L); cap)
        stroke.start_cap = cap.value();
    lua_getfield(L, -1, "end_cap");
    if (auto cap = parse_cap_style(L); cap)
        stroke.end_cap = cap.value();
    lua_getfield(L, -1, "dash_cap");
    if (auto cap = parse_cap_style(L); cap)
        stroke.dash_cap = cap.value();
    lua_getfield(L, -1, "line_join");
    if (auto join = parse_line_join(L); join)
        stroke.line_join = join.value();
    if (auto miter_limit = table_getf(L, "miter_limit"); miter_limit)
        stroke.miter_limit = miter_limit.value();
    lua_getfield(L, -1, "dash");
    if (auto dash = parse_dash_style(L); dash) {
        auto [dash_style, dash_pattern] = std::move(dash.value());
        stroke.dash_style = dash_style;
        stroke.dash_pattern = std::move(dash_pattern);
    }
    if (auto dash_offset = table_getf(L, "dash_offset"); dash_offset)
        stroke.dash_offset = dash_offset.value();
    return stroke;
}

void create_table_from_stroke(lua_State* L, const Shape::StrokeStyle& stroke) {
    lua_createtable(L, 0, 8);
    lua_pushnumber(L, stroke.width);
    lua_setfield(L, -2, "width");
    unparse_cap_style(L, stroke.start_cap);
    lua_setfield(L, -2, "start_cap");
    unparse_cap_style(L, stroke.end_cap);
    lua_setfield(L, -2, "end_cap");
    unparse_cap_style(L, stroke.dash_cap);
    lua_setfield(L, -2, "dash_cap");
    unparse_line_join(L, stroke.line_join);
    lua_setfield(L, -2, "line_join");
    lua_pushnumber(L, stroke.miter_limit);
    lua_setfield(L, -2, "miter_limit");
    unparse_dash_style(L, stroke.dash_style, stroke.dash_pattern);
    lua_setfield(L, -2, "dash");
    lua_pushnumber(L, stroke.dash_offset);
    lua_setfield(L, -2, "dash_offset");
}

int shape_widen(lua_State* L) {
    lua_settop(L, 2);
    auto shape = pull_shape(L, 1);
    auto stroke = create_stroke_from_table(L);
    bool outline = false;
    if (!lua_isnumber(L, 2)) {
        lua_getfield(L, 2, "outline");
        outline = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }

    auto tolerance = get_flattening_tolerance();
    new_shape(L, (outline ? shape->outline(tolerance) : *shape).widen(stroke, tolerance));

    return 1;
}

luaL_Reg shape_methods[] = {
    {"transform", shape_transform},
    {"subset", shape_subset},
    {"get_path", shape_get_path},
    {"set_path", shape_set_path},
    {"stream", shape_stream},
    {"copy", shape_copy},
    {"move_to", shape_move_to},
    {"lines_to", shape_lines_to},
    {"beziers_to", shape_beziers_to},
    {"close_path", shape_close_path},
    {"anchor", shape_anchor},
    {"bounds", shape_bounds},
    {"combine", shape_combine},
    {"compare", shape_compare},
    {"contains", shape_contains},
    {"compute_area", shape_area},
    {"compute_length", shape_length},
    {"point_at_length", shape_point_at_length},
    {"flatten", shape_flatten},
    {"outline", shape_outline},
    {"widen", shape_widen},
    {"minmax", shape_minmax},
    {nullptr, nullptr}};

int shape_new(lua_State* L) {
    if (lua_gettop(L) == 0) {
        new_shape(L);
        return 1;
    }
    lua_settop(L, 1);
    new_shape(L);
    lua_pushcfunction(L, shape_set_path);
    auto len = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= len; ++i) {
        lua_pushvalue(L, 3);
        lua_pushvalue(L, 2);
        lua_pushinteger(L, i);
        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        lua_pushinteger(L, 1);
        lua_gettable(L, -2);
        lua_pushinteger(L, 2);
        lua_gettable(L, -3);
        lua_remove(L, -3);
        lua_call(L, 4, 0);
    }
    lua_settop(L, 2);
    return 1;
}

int shape_fromstring(lua_State* L) {
    std::istringstream is(luaL_checkstring(L, 1));
    is >> *new_shape(L);
    return 1;
}

luaL_Reg shape_libfuncs[] = {{"new", shape_new}, {"fromstring", shape_fromstring}, {nullptr, nullptr}};

void set_methods(lua_State* L, luaL_Reg* methods) {
    lua_createtable(L, 0, 1);
    lua_pushlightuserdata(L, methods);
    lua_pushcclosure(L, generic_getmethod, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
}

void create_shape_lib(lua_State* L) {
    if (luaL_newmetatable(L, MAKE_TNAME(Shape))) {
        lua_pushlightuserdata(L, shape_methods);
        luaL_setfuncs(L, shape_metatable, 1);
        lua_pop(L, 1);
    }
    luaL_newlib(L, shape_libfuncs);
    set_methods(L, shape_methods);
}

luaL_Reg text_layout_metatable[] = {{"__gc", generic_dtor<TextLayout>}, {"__index", generic_getmethod}, {nullptr, nullptr}};

[[nodiscard]] TextLayout* pull_layout(lua_State* L, int idx) {
    return static_cast<TextLayout*>(luaL_checkudata(L, idx, MAKE_TNAME(TextLayout)));
}

template <typename... Args> TextLayout* new_layout(lua_State* L, Args&&... args) {
    auto layout = static_cast<TextLayout*>(lua_newuserdata(L, sizeof(TextLayout)));
    new (layout) TextLayout(std::forward<Args>(args)...);
    luaL_setmetatable(L, MAKE_TNAME(TextLayout));
    return layout;
}

int draw_text_layout(lua_State* L) {
    auto tl = pull_layout(L, 1);

    bool lt_anchor = false, rb_anchor = false;
    if (!lua_isnoneornil(L, 2)) {
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "lt");
            lua_getfield(L, 2, "rb");
            lt_anchor = lua_toboolean(L, -2);
            rb_anchor = lua_toboolean(L, -1);
            lua_pop(L, 2);
        } else
            lt_anchor = rb_anchor = lua_toboolean(L, 2);
    }

    auto shape = new_shape(L, tl->draw());

    if (lt_anchor || rb_anchor) {
        auto tm = tl->metrics();
        Box anchor {};
        if (lt_anchor) {
            anchor.left = tm.left;
            anchor.top = tm.top;
        }
        if (rb_anchor) {
            anchor.width = tm.width;
            anchor.height = tm.height;
        }
        *shape = shape->anchor(anchor);
    }

    return 1;
}

int get_text_metrics(lua_State* L) {
    lua_settop(L, 1);
    auto tl = pull_layout(L, 1);

    auto tm = tl->metrics();
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, static_cast<int>(tm.line_count));
    lua_pushnumber(L, tm.height);
    lua_pushnumber(L, tm.width);
    lua_pushnumber(L, tm.top);
    lua_pushnumber(L, tm.left);
    lua_setfield(L, 2, "left");
    lua_setfield(L, 2, "top");
    lua_setfield(L, 2, "width");
    lua_setfield(L, 2, "height");
    lua_setfield(L, 2, "line_count");

    return 1;
}

TextStyle create_textstyle_from_table(lua_State* L) {
    TextStyle ts;

    ts.fn = table_gets(L, "fn");
    if (!ts.fn)
        ts.fn = table_gets(L, "fontname");

    ts.fs = table_getf(L, "fs");
    if (!ts.fs)
        ts.fs = table_getf(L, "fontsize");

    ts.locale = table_gets(L, "locale");

    auto opt_weight = table_getf(L, "b");
    if (!opt_weight)
        opt_weight = table_getb(L, "bold");
    if (opt_weight) {
        auto weight = opt_weight.value();
        if (weight == 1)
            weight = 700;
        else if (weight == 0)
            weight = 400;
        ts.axis_values.emplace_back(DWRITE_FONT_AXIS_TAG_WEIGHT, weight);
    }

    auto opt_italic = table_getf(L, "i");
    if (!opt_italic)
        opt_italic = table_getb(L, "italic");
    if (opt_italic)
        ts.axis_values.emplace_back(DWRITE_FONT_AXIS_TAG_ITALIC, opt_italic.value());

    lua_getfield(L, -1, "fvar");
    if (!lua_isnil(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            auto tag = lua_tostring(L, -2);
            if (!tag || strlen(tag) != 4)
                llerror(L, "font axis tag must be a string of 4 chars");
            auto value = static_cast<float>(lua_tonumber(L, -1));

            ts.axis_values.emplace_back(DWRITE_MAKE_FONT_AXIS_TAG(tag[0], tag[1], tag[2], tag[3]), value);

            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    ts.u = table_getf(L, "u");
    if (!ts.u)
        ts.u = table_getb(L, "underline");

    ts.s = table_getf(L, "s");
    if (!ts.s)
        ts.s = table_getb(L, "strikeout");

    ts.fsp = table_getf(L, "fsp");
    if (!ts.fsp)
        ts.fsp = table_getf(L, "spacing");

    lua_getfield(L, -1, "dir");
    auto dtype = lua_type(L, -1);
    if (dtype == LUA_TNUMBER) {
        auto d = static_cast<int>(lua_tointeger(L, -1));
        ts.dir = std::make_pair(d, d / 2 * 2);
    } else if (dtype == LUA_TTABLE) {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -2, 2);
        ts.dir = std::make_pair(static_cast<int>(lua_tointeger(L, -2)), static_cast<int>(lua_tointeger(L, -1)));
        lua_pop(L, 2);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "lbox");
    if (!lua_isnil(L, -1)) {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -2, 2);
        ts.lbox = std::make_pair(static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1)));
        lua_pop(L, 2);
    }
    lua_pop(L, 1);

    if (auto opt_wrap = table_gets(L, "wrap"); opt_wrap) {
        auto wrap = opt_wrap.value();
        if (wrap == "no")
            ts.wrap = DWRITE_WORD_WRAPPING_NO_WRAP;
        else if (wrap == "word")
            ts.wrap = DWRITE_WORD_WRAPPING_WHOLE_WORD;
        else if (wrap == "break")
            ts.wrap = DWRITE_WORD_WRAPPING_EMERGENCY_BREAK;
        else if (wrap == "char")
            ts.wrap = DWRITE_WORD_WRAPPING_CHARACTER;
    }

    if (auto an = table_getf(L, "an"); an)
        ts.an = static_cast<int>(an.value());
    else if (an = table_getf(L, "align"); an)
        ts.an = static_cast<int>(an.value());

    if (auto opt_alignment = table_gets(L, "align"); opt_alignment) {
        auto alignment = opt_alignment.value();
        if (alignment == "leading")
            ts.alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
        else if (alignment == "trailing")
            ts.alignment = DWRITE_TEXT_ALIGNMENT_TRAILING;
        else if (alignment == "center")
            ts.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        else if (alignment == "justified")
            ts.alignment = DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
    }

    lua_getfield(L, -1, "range");
    if (!lua_isnil(L, -1)) {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -2, 2);
        ts.range = std::make_pair(static_cast<uint32_t>(lua_tointeger(L, -2)) - 1, static_cast<uint32_t>(lua_tointeger(L, -1)));
        lua_pop(L, 2);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "feat");
    if (!lua_isnil(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            auto tag = lua_tostring(L, -2);
            if (!tag || strlen(tag) != 4)
                llerror(L, "font feature tag must be a string of 4 chars");
            auto value = static_cast<uint32_t>(lua_tointeger(L, -1));

            ts.feat.emplace_back(DWRITE_MAKE_FONT_FEATURE_TAG(tag[0], tag[1], tag[2], tag[3]), value);

            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    ts.kern = table_getf(L, "kern");

    ts.lsp = table_getf(L, "lsp");

    ts.dip = table_getb(L, "dip").value_or(false);

    return ts;
}

int layout_set_style(lua_State* L) {
    lua_settop(L, 2);
    auto tl = pull_layout(L, 1);
    auto ts = create_textstyle_from_table(L);
    tl->set_style(ts);
    return 0;
}

luaL_Reg text_layout_methods[] = {{"to_shape", draw_text_layout}, {"metrics", get_text_metrics}, {"set_style", layout_set_style}, {nullptr, nullptr}};

int layout_plaintext(lua_State* L) {
    lua_settop(L, 2);
    auto us = luaL_checkstring(L, 1);

    TextStyle ts;
    if (!lua_isnil(L, 2))
        ts = create_textstyle_from_table(L);

    new_layout(L, us, ts);

    return 1;
}

luaL_Reg layout_libfuncs[] = {
    {"from_plaintext", layout_plaintext},
    {nullptr, nullptr},
};

void create_layout_lib(lua_State* L) {
    if (luaL_newmetatable(L, MAKE_TNAME(TextLayout))) {
        lua_pushlightuserdata(L, text_layout_methods);
        luaL_setfuncs(L, text_layout_metatable, 1);
        lua_pop(L, 1);
    }
    luaL_newlib(L, layout_libfuncs);
    set_methods(L, text_layout_methods);
}

[[nodiscard]] Color pull_color(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    auto type = lua_type(L, idx);
    if (type == LUA_TNIL)
        return Color::none();
    if (type == LUA_TNUMBER) {
        Color color {0, 0, 0, 0};
        reinterpret_cast<uint32_t&>(color.r) = static_cast<uint32_t>(lua_tointeger(L, idx));
        return color;
    }
    if (type == LUA_TSTRING) {
        return Color::from_hex(lua_tostring(L, idx));
    }
    lua_getfield(L, idx, "r");
    lua_getfield(L, idx, "g");
    lua_getfield(L, idx, "b");
    lua_getfield(L, idx, "a");
    if (lua_isnil(L, -4) && lua_isnil(L, -3) && lua_isnil(L, -2) && lua_isnil(L, -1)) {
        lua_pop(L, 4);
        lua_rawgeti(L, idx, 1);
        lua_rawgeti(L, idx, 2);
        lua_rawgeti(L, idx, 3);
        lua_rawgeti(L, idx, 4);
    }
    Color color = lua_isnil(L, -4) && lua_isnil(L, -3) && lua_isnil(L, -2) ? Color::current_color(static_cast<unsigned char>(lua_tointeger(L, -1)))
                                                                           : Color {
                                                                                 static_cast<unsigned char>(lua_tointeger(L, -4)),
                                                                                 static_cast<unsigned char>(lua_tointeger(L, -3)),
                                                                                 static_cast<unsigned char>(lua_tointeger(L, -2)),
                                                                                 static_cast<unsigned char>(lua_tointeger(L, -1)),
                                                                             };
    lua_pop(L, 4);
    return color;
}

void new_color(lua_State* L, Color color) {
    if (color.is_none())
        lua_pushnil(L);
    else if (color.is_current_color()) {
        lua_createtable(L, 0, 1);
        lua_pushinteger(L, color.a);
        lua_setfield(L, -2, "a");
        luaL_setmetatable(L, MAKE_TNAME(Color));
    } else {
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, color.r);
        lua_setfield(L, -2, "r");
        lua_pushinteger(L, color.g);
        lua_setfield(L, -2, "g");
        lua_pushinteger(L, color.b);
        lua_setfield(L, -2, "b");
        lua_pushinteger(L, color.a);
        lua_setfield(L, -2, "a");
        luaL_setmetatable(L, MAKE_TNAME(Color));
    }
}

int color_to_hex_bgr(lua_State* L) {
    push_string(L, pull_color(L, 1).to_hex_bgr());
    return 1;
}

int color_to_hex_alpha(lua_State* L) {
    push_string(L, pull_color(L, 1).to_hex_alpha());
    return 1;
}

int color_new(lua_State* L) {
    lua_settop(L, 4);
    lua_createtable(L, 0, 4);
    lua_insert(L, 1);
    lua_setfield(L, 1, "r");
    lua_setfield(L, 1, "g");
    lua_setfield(L, 1, "b");
    lua_setfield(L, 1, "a");
    luaL_setmetatable(L, MAKE_TNAME(Color));
    return 1;
}

int color_from_hex(lua_State* L) {
    new_color(L, Color::from_hex(luaL_checkstring(L, 1)));
    return 1;
}

luaL_Reg color_methods[] = {
    {"to_hex_bgr", color_to_hex_bgr},
    {"to_hex_alpha", color_to_hex_alpha},
    {nullptr, nullptr},
};

luaL_Reg color_metatable[] = {
    {"__index", generic_getmethod},
    {nullptr, nullptr},
};

luaL_Reg color_libfuncs[] = {
    {"new", color_new},
    {"from_hex", color_from_hex},
    {nullptr, nullptr},
};

[[nodiscard]] Point pull_point(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    lua_rawgeti(L, idx, 1);
    lua_rawgeti(L, idx, 2);
    Point point {
        static_cast<float>(lua_tonumber(L, -2)),
        static_cast<float>(lua_tonumber(L, -1)),
    };
    lua_pop(L, 2);
    return point;
}

void new_point(lua_State* L, Point point) {
    lua_createtable(L, 2, 0);
    lua_pushnumber(L, point.x);
    lua_rawseti(L, -2, 1);
    lua_pushnumber(L, point.y);
    lua_rawseti(L, -2, 2);
}

[[nodiscard]] Line pull_line(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    lua_getfield(L, idx, "draw");
    lua_getfield(L, idx, "clip");
    lua_getfield(L, idx, "color");
    lua_getfield(L, idx, "pos");
    Line line {
        cow_ptr<Shape>(pull_shape(L, -4)),
        lua_isnil(L, -3) ? make_cow<Shape>() : cow_ptr<Shape>(pull_shape(L, -3)),
        pull_color(L, -2),
        pull_point(L, -1),
    };
    lua_pop(L, 3);
    return line;
}

int line_tostring(lua_State* L) {
    push_string(L, pull_line(L, 1).tostring(mskfunc_ctx.decimal_places));
    return 1;
}

int line_new(lua_State* L) {
    lua_settop(L, 4);
    lua_createtable(L, 0, 4);
    lua_insert(L, 1);
    lua_setfield(L, 1, "pos");
    lua_setfield(L, 1, "color");
    lua_setfield(L, 1, "clip");
    lua_setfield(L, 1, "draw");
    luaL_setmetatable(L, MAKE_TNAME(Line));
    return 1;
}

luaL_Reg line_metatable[] = {
    {"__tostring", line_tostring},
    {nullptr, nullptr},
};

luaL_Reg line_libfuncs[] = {
    {"new", line_new},
    {"tostring", line_tostring},
    {nullptr, nullptr},
};

void new_line(lua_State* L, Line line) {
    lua_createtable(L, 0, 4);
    new_shape(L, line.draw.into_owned());
    lua_setfield(L, -2, "draw");
    new_shape(L, line.clip.into_owned());
    lua_setfield(L, -2, "clip");
    new_color(L, line.color);
    lua_setfield(L, -2, "color");
    new_point(L, line.pos);
    lua_setfield(L, -2, "pos");
    luaL_setmetatable(L, MAKE_TNAME(Line));
}

template <typename... Args> void new_composition(lua_State* L, Args&&... args) {
    typedef std::shared_ptr<const Composition> PComp;
    auto comp = static_cast<PComp*>(lua_newuserdata(L, sizeof(PComp)));
    new (comp) PComp(std::forward<Args>(args)...);
    luaL_setmetatable(L, MAKE_TNAME(Composition));
    if (!*comp) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
}

std::shared_ptr<const Composition> pull_composition(lua_State* L, int idx) {
    return *static_cast<std::shared_ptr<const Composition>*>(luaL_checkudata(L, idx, MAKE_TNAME(Composition)));
}

int composition_to_shape(lua_State* L) {
    auto comp = pull_composition(L, 1);
    new_shape(L, comp->to_shape(get_flattening_tolerance()));
    return 1;
}

int composition_to_lines(lua_State* L) {
    auto comp = pull_composition(L, 1);
    auto lines = comp->to_lines(pull_point(L, 2), luaL_checkint(L, 3), get_flattening_tolerance());
    lua_createtable(L, static_cast<int>(lines.size()), 0);
    int i = 0;
    for (auto& line : lines) {
        new_line(L, std::move(line));
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

int composition_get_parent(lua_State* L) {
    auto comp = pull_composition(L, 1);
    new_composition(L, comp->parent);
    return 1;
}

int composition_get_shape(lua_State* L) {
    auto comp = pull_composition(L, 1);
    new_shape(L, comp->shape);
    return 1;
}

[[nodiscard]] Context pull_context(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    lua_getfield(L, idx, "anchor");
    lua_getfield(L, idx, "transform");
    lua_getfield(L, idx, "fill");
    lua_getfield(L, idx, "stroke");
    lua_getfield(L, idx, "mask");
    lua_getfield(L, idx, "stroke_style");
    Context cc {
        pull_box(L, -6),   lua_isnil(L, -5) ? Transform::identity() : pull_transform(L, -5),       pull_color(L, -4),
        pull_color(L, -3), lua_isnil(L, -1) ? Shape::StrokeStyle {} : create_stroke_from_table(L), lua_isnil(L, -2) ? nullptr : pull_composition(L, -2),
    };
    lua_pop(L, 6);
    return cc;
}

void new_context(lua_State* L, const Context& context) {
    lua_createtable(L, 0, 6);
    new_box(L, context.anchor);
    lua_setfield(L, -2, "anchor");
    new_transform(L, context.transform);
    lua_setfield(L, -2, "transform");
    new_color(L, context.fill);
    lua_setfield(L, -2, "fill");
    new_color(L, context.stroke);
    lua_setfield(L, -2, "stroke");
    create_table_from_stroke(L, context.stroke_style);
    lua_setfield(L, -2, "stroke_style");
    new_composition(L, context.mask);
    lua_setfield(L, -2, "mask");
}

int composition_get_context(lua_State* L) {
    new_context(L, pull_composition(L, 1)->context);
    return 1;
}

int composition_stream(lua_State* L) {
    typedef std::shared_ptr<const Composition> PComp;
    std::vector<PComp> stack;
    stack.emplace_back(pull_composition(L, 1));
    for (auto parent = stack.back()->parent; parent; parent = stack.back()->parent)
        stack.emplace_back(std::move(parent));
    PComp comp;
    while (!stack.empty()) {
        auto cur = std::move(stack.back());
        stack.pop_back();
        if (cur->parent != comp)
            cur = std::make_shared<const Composition>(cur->shape, cur->context, comp);
        lua_pushvalue(L, 2);
        new_composition(L, cur);
        lua_call(L, 1, 1);
        if (lua_isnil(L, -1))
            comp = std::move(cur);
        else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
            ;
        else
            comp = pull_composition(L, -1);
    }
    new_composition(L, comp);
    return 1;
}

luaL_Reg composition_methods[] = {
    {"to_shape", composition_to_shape},
    {"to_lines", composition_to_lines},
    {"get_parent", composition_get_parent},
    {"get_shape", composition_get_shape},
    {"get_context", composition_get_context},
    {"stream", composition_stream},
    {nullptr, nullptr},
};

luaL_Reg composition_metatable[] = {
    {"__gc", generic_dtor<std::shared_ptr<const Composition>>},
    {"__index", generic_getmethod},
    {nullptr, nullptr},
};

int load_svg_as_composition(lua_State* L) {
    std::size_t len;
    auto s = luaL_checklstring(L, 1, &len);
    auto comp = Svg::load({s, len});
    new_composition(L, std::move(comp));
    return 1;
}

int composition_new(lua_State* L) {
    new_composition(L, new Composition {*pull_shape(L, 1), pull_context(L, 2), lua_isnoneornil(L, 3) ? nullptr : pull_composition(L, 3)});
    return 1;
}

luaL_Reg composition_libfuncs[] = {
    {"new", composition_new},
    {"load_svg", load_svg_as_composition},
    {nullptr, nullptr},
};

void create_composition_lib(lua_State* L) {
    if (luaL_newmetatable(L, MAKE_TNAME(Color))) {
        lua_pushlightuserdata(L, color_methods);
        luaL_setfuncs(L, color_metatable, 1);
        lua_pop(L, 1);
    }
    if (luaL_newmetatable(L, MAKE_TNAME(Line))) {
        luaL_setfuncs(L, line_metatable, 0);
        lua_pop(L, 1);
    }
    if (luaL_newmetatable(L, MAKE_TNAME(Composition))) {
        lua_pushlightuserdata(L, composition_methods);
        luaL_setfuncs(L, composition_metatable, 1);
        lua_pop(L, 1);
    }
    luaL_newlib(L, composition_libfuncs);
    set_methods(L, composition_methods);
    luaL_newlib(L, color_libfuncs);
    set_methods(L, color_methods);
    lua_setfield(L, -2, "color");
    luaL_newlib(L, line_libfuncs);
    lua_setfield(L, -2, "line");
}

void create_transform_lib(lua_State* L) {
    if (luaL_loadbuffer(L, reinterpret_cast<const char*>(luaJIT_BC_transform_lib), luaJIT_BC_transform_lib_SIZE, "transform") != LUA_OK)
        lerror(L);
    lua_call(L, 0, 1);
}

int hack_karaskel(lua_State* L) {
    lua_settop(L, 1);
    if (lua_isnil(L, 1)) {
        lua_pop(L, 1);
        lua_getglobal(L, "karaskel");
    }

    lua_getfield(L, 1, "collect_head");
    lua_getfenv(L, -1);

    if (luaL_loadbuffer(L, reinterpret_cast<const char*>(luaJIT_BC_run_text_template), luaJIT_BC_run_text_template_SIZE, "patched run_text_template") != LUA_OK)
        lerror(L);
    lua_call(L, 0, 1);

    lua_setfield(L, -2, "run_text_template");

    return 0;
}

int utf8_to_utf16(lua_State* L) {
    auto us = luaL_checkstring(L, 1);

    auto ws = u2w(us);
    lua_pushlstring(L, reinterpret_cast<char*>(ws.data()), ws.size() * 2);

    return 1;
}

int utf16_to_utf8(lua_State* L) {
    std::size_t len;
    auto ws = reinterpret_cast<const wchar_t*>(luaL_checklstring(L, 1, &len));
    if (len % 2 != 0)
        llerror(L, "not a valid UTF-16 string");

    auto us = w2u(std::wstring(ws, ws + len / 2));
    lua_pushlstring(L, us.data(), us.size());

    return 1;
}

int load_svg_as_shape(lua_State* L) {
    auto s = luaL_checkstring(L, 1);
    auto comp = Svg::load(s);
    new_shape(L, comp ? comp->to_shape(get_flattening_tolerance()) : Shape {});
    return 1;
}

#ifdef _DEBUG
int inner_condom(lua_State* L, lua_CFunction f) {
    try {
        return f(L);
    } catch (std::exception& e) { lua_pushstring(L, e.what()); } catch (...) {
        lua_pushliteral(L, "C++ exception");
    }
    return -1;
}

int condom(lua_State* L, lua_CFunction f) {
    __try {
        if (auto r = inner_condom(L, f); r != -1)
            return r;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        lua_pushliteral(L, "SEH exception: access violation");
    }
    return lua_error(L);
}

int install_condom(lua_State* L) {
    lua_pushlightuserdata(L, reinterpret_cast<void*>(condom));
    luaJIT_setmode(L, -1, LUAJIT_MODE_WRAPCFUNC | LUAJIT_MODE_ON);
    return 0;
}
#endif

luaL_Reg misc_libfuncs[] = {
    {"hack_karaskel", hack_karaskel},
    {"utf8_to_utf16", utf8_to_utf16},
    {"utf16_to_utf8", utf16_to_utf8},
    {"load_svg_as_shape", load_svg_as_shape},
#ifdef _DEBUG
    {"install_condom", install_condom},
#endif
    {nullptr, nullptr}};

void create_misc_lib(lua_State* L) {
    luaL_newlib(L, misc_libfuncs);
}

extern "C" __declspec(dllexport) int luaopen_mskfunc(lua_State* L) {
    lua_newtable(L);

    lua_pushliteral(L, "layout");
    create_layout_lib(L);
    lua_rawset(L, -3);

    lua_pushliteral(L, "shape");
    create_shape_lib(L);
    lua_rawset(L, -3);

    lua_pushliteral(L, "transform");
    create_transform_lib(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "mskfunc.transform");
    lua_rawset(L, -3);

    lua_pushliteral(L, "composition");
    create_composition_lib(L);
    lua_rawset(L, -3);

    lua_pushliteral(L, "misc");
    create_misc_lib(L);
    lua_rawset(L, -3);

    lua_pushliteral(L, "context");
    create_context_lib(L);
    lua_rawset(L, -3);

    return 1;
}
