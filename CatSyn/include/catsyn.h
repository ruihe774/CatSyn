#pragma once

#include <array>
#include <atomic>
#include <new>
#include <utility>

#ifdef _WIN32
#ifdef CAT_IMPL
#define CAT_API __declspec(dllexport)
#else
#define CAT_API __declspec(dllimport)
#endif
#else
#ifdef CAT_IMPL
#define CAT_API __attribute__ ((visibility ("default")))
#else
#define CAT_API
#endif
#endif

namespace catsyn {

class IObject {
    mutable std::atomic_size_t refcount{0};

  public:
    void add_ref() const noexcept {
        refcount.fetch_add(1, std::memory_order_relaxed);
    }

    void release() const noexcept {
        auto rc = refcount.fetch_sub(1, std::memory_order_release) - 1;
        if (rc == 0) {
            std::atomic_thread_fence(std::memory_order_acquire);
            const_cast<IObject*>(this)->drop();
        }
    }

    bool is_unique() const noexcept {
        return refcount.load(std::memory_order_acq_rel) == 1;
    }

    virtual void clone(IObject** out) const noexcept = 0;

    virtual ~IObject() = default;

  protected:
    virtual void drop() noexcept = 0;
};

class ITable : virtual public IObject {
  public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    virtual const IObject* get(size_t ref) const noexcept = 0;
    virtual void set(size_t ref, const IObject* obj) noexcept = 0;

    virtual size_t get_ref(const char* key) const noexcept = 0;
    virtual const char* get_key(size_t ref) const noexcept = 0;
    virtual void set_key(size_t ref, const char* key) noexcept = 0;

    virtual size_t size() const noexcept = 0;
};

class IBytes : virtual public IObject {
  protected:
    void* buf{nullptr};
    size_t len{0};

  public:
    void* data() noexcept {
        return buf;
    }

    const void* data() const noexcept {
        return buf;
    }

    size_t size() const noexcept {
        return len;
    }

    virtual void realloc(size_t new_size) noexcept = 0;
};

class IAlignedBytes : virtual public IBytes {
  public:
    static constexpr std::align_val_t alignment = static_cast<std::align_val_t>(64);
};

enum class SampleType {
    Integer,
    Float,
};

class INumberArray : virtual public IBytes {
  public:
    SampleType sample_type;
};

struct FrameInfo;
class IFrame;
class IFactory;
class ILogger;
class IEnzymeFinder;
class IEnzymeAdapter;

class INucleus : virtual public IObject {
  public:
    virtual void calling_thread_init() noexcept = 0;

    virtual IFactory* get_factory() noexcept = 0;
    virtual ILogger* get_logger() noexcept = 0;

    virtual ITable* get_enzyme_finders() noexcept = 0;
    virtual ITable* get_enzyme_adapters() noexcept = 0;
};

class IFactory : virtual public IObject {
  public:
    virtual void create_bytes(const void* data, size_t len, IBytes** out) noexcept = 0;
    virtual void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept = 0;
    virtual void create_number_array(SampleType sample_type, const void* data, size_t len, INumberArray** out) noexcept = 0;
    virtual void create_frame(FrameInfo fi, const IAlignedBytes** planes, const size_t* strides, const ITable* props,
                              IFrame** out) noexcept = 0;
    virtual void create_table(size_t reserve_capacity, ITable** out) noexcept = 0;

    virtual void create_dll_enzyme_finder(const char* path, IEnzymeFinder** out) noexcept = 0;
    virtual void create_catsyn_v1_enzyme_adapter(IEnzymeAdapter** out) noexcept = 0;
};

enum class ColorFamily {
    Gray = 1,
    RGB,
    YUV,
};

union FrameFormat {
    uint32_t id;
    struct {
        unsigned height_subsampling : 8;
        unsigned width_subsampling : 8;
        unsigned bits_per_sample : 8;
        SampleType sample_type : 4;
        ColorFamily color_family : 4;
    } detail;
};

struct FrameInfo {
    FrameFormat format;
    unsigned width;
    unsigned height;
};

struct FpsFraction {
    unsigned num;
    unsigned den;
};

struct VideoInfo {
    FrameInfo frame_info;
    FpsFraction fps;
    size_t frame_count;
};

class IFrame : virtual public IObject {
  public:
    virtual const IAlignedBytes* get_plane(unsigned idx) const noexcept = 0;
    virtual IAlignedBytes* get_plane_mut(unsigned idx) noexcept = 0;
    virtual void set_plane(unsigned idx, const IAlignedBytes* in, size_t stride) noexcept = 0;

    virtual FrameInfo get_frame_info() const noexcept = 0;

    virtual size_t get_stride(unsigned idx) const noexcept = 0;

    virtual const ITable* get_frame_props() const noexcept = 0;
    virtual ITable* get_frame_props_mut() noexcept = 0;
    virtual void set_frame_props(const ITable* props) noexcept = 0;
};

enum class LogLevel { DEBUG = 10, INFO = 20, WARNING = 30 };

class ILogSink : virtual public IObject {
  public:
    virtual void send_log(LogLevel level, const char* msg) noexcept = 0;
};

class ILogger : virtual public IObject {
  public:
    virtual void log(LogLevel level, const char* msg) const noexcept = 0;
    virtual void set_level(LogLevel level) noexcept = 0;
    virtual void set_sink(ILogSink* in) noexcept = 0;
};

class IEnzyme : virtual public IObject {};

class IEnzymeFinder : virtual public IObject {
  public:
    virtual size_t find_enzyme(const char* const** out) const noexcept = 0;
};

class IEnzymeAdapter : virtual public IObject {
  public:
    virtual void load_enzyme(const char* token, IObject** out) const noexcept = 0;
};

CAT_API void create_nucleus(INucleus** out);

} // namespace catsyn
