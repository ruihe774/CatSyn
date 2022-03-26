#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <typeinfo>

#ifdef _WIN32
#define CAT_EXPORT __declspec(dllexport)
#define CAT_IMPORT __declspec(dllimport)
#else
#define CAT_EXPORT __attribute__((visibility("default")))
#define CAT_IMPORT
#endif
#ifdef CAT_IMPL
#define CAT_API CAT_EXPORT
#else
#define CAT_API CAT_IMPORT
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

  protected:
    size_t acquire_refcount() const noexcept {
        return refcount.load(std::memory_order_acquire);
    }

  public:
    bool is_unique() const noexcept {
        return acquire_refcount() == 1;
    }

    virtual void clone(IObject** out) const noexcept = 0;

    virtual ~IObject() = default;

  protected:
    virtual void drop() noexcept = 0;
};

class IRef : virtual public IObject {
  public:
    void clone(IObject** out) const noexcept final {
        *out = dynamic_cast<IObject*>(const_cast<IRef*>(this));
        this->add_ref();
    }
};

enum class LogLevel {
    DEBUG = 10,
    INFO = 20,
    WARNING = 30
};

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

class ITable : virtual public IObject {
  public:
    virtual const IObject* get(size_t ref, const char** key_out) const noexcept = 0;
    virtual void set(size_t ref, const IObject* obj, const char* key) noexcept = 0;
    virtual size_t erase(size_t ref) noexcept = 0;
    virtual size_t find(const char* key) const noexcept = 0;
    virtual size_t size() const noexcept = 0;
    virtual void clear() noexcept = 0;
    virtual size_t begin() const noexcept = 0;
    virtual size_t end() const noexcept = 0;
    virtual size_t next(size_t ref) const noexcept = 0;
    virtual size_t prev(size_t ref) const noexcept = 0;
};

class IBytes : virtual public IObject {
  public:
    virtual void* data() noexcept = 0;
    virtual const void* data() const noexcept = 0;
    virtual size_t size() const noexcept = 0;
    virtual void realloc(size_t new_size) noexcept = 0;
};

enum class SampleType {
    Integer,
    Float,
};

class IArray : virtual public IBytes {
    using IBytes::size;

  public:
    size_t bytes_count() const noexcept {
        return size();
    }

    virtual const std::type_info& get_element_type() const noexcept = 0;
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
    virtual const IBytes* get_plane(unsigned idx) const noexcept = 0;
    virtual IBytes* get_plane_mut(unsigned idx) noexcept = 0;
    virtual void set_plane(unsigned idx, const IBytes* in, size_t stride) noexcept = 0;

    virtual FrameInfo get_frame_info() const noexcept = 0;

    virtual size_t get_stride(unsigned idx) const noexcept = 0;

    virtual const ITable* get_frame_props() const noexcept = 0;
    virtual ITable* get_frame_props_mut() noexcept = 0;
    virtual void set_frame_props(const ITable* props) noexcept = 0;
};

class IEnzymeFinder : virtual public IRef {
  public:
    virtual const char* const* find(size_t* len) noexcept = 0;
};

class IRibosome : virtual public IRef {
  public:
    virtual const char* get_identifier() const noexcept = 0;
    virtual void synthesize_enzyme(const char* token, IObject** out) noexcept = 0;
    virtual void hydrolyze_enzyme(IObject** inout) noexcept = 0;
};

class IEnzyme : virtual public IRef {
  public:
    virtual const char* get_identifier() const noexcept = 0;
    virtual const char* get_namespace() const noexcept = 0;
    virtual const ITable* get_functions() const noexcept = 0;
};

class IFactory : virtual public IRef {
  public:
    virtual void create_bytes(const void* data, size_t len, IBytes** out) noexcept = 0;
    virtual void create_array(const std::type_info& type, const void* data, size_t bytes_count,
                              IArray** out) noexcept = 0;
    virtual void create_frame(FrameInfo fi, const IBytes** planes, const size_t* strides, const ITable* props,
                              IFrame** out) noexcept = 0;
    virtual void create_table(size_t reserve_capacity, ITable** out) noexcept = 0;

    virtual void create_dll_enzyme_finder(const char* path, IEnzymeFinder** out) noexcept = 0;
    virtual void create_catsyn_v1_ribosome(IRibosome** out) noexcept = 0;
};

struct ArgSpec {
    const char* name;
    const std::type_info& type;
    bool array;
    bool required;
};

class IFunction : virtual public IRef {
  public:
    virtual void invoke(ITable* args, const IObject** out) = 0;
    virtual const ArgSpec* get_arg_specs(size_t* len) const noexcept = 0;
    virtual const std::type_info& get_out_type() const noexcept = 0;
};

class INucleus;

class ISubstrate : virtual public IRef {
  public:
    virtual VideoInfo get_video_info() const noexcept = 0;
    virtual INucleus* get_nucleus() noexcept = 0;
};

enum FilterFlags {
    ffNormal = 0,
    ffMakeLinear = 4,
    ffSingleThreaded = 8,
};

struct FrameSource {
    const ISubstrate* substrate;
    size_t frame_idx;
};

struct FrameData {
    const FrameSource* dependencies;
    size_t dependency_count;
};

class IFilter : virtual public IRef {
  public:
    virtual FilterFlags get_filter_flags() const noexcept = 0;
    virtual VideoInfo get_video_info() const noexcept = 0;
    virtual void get_frame_data(size_t frame_idx, FrameData** frame_data) const noexcept = 0;
    virtual void process_frame(const IFrame* const* input_frames, FrameData** frame_data, const IFrame** out) const = 0;
    virtual void drop_frame_data(FrameData* frame_data) const noexcept = 0;
};

class IOutput : virtual public IRef {
  public:
    typedef std::function<void(const IFrame* frame, std::exception_ptr exc)> Callback;
    virtual void get_frame(size_t frame_idx, Callback cb) noexcept = 0;
};

struct NucleusConfig {
    unsigned thread_count;
    unsigned mem_hint_mb;
};

struct Version {
    uint16_t minor;
    uint16_t patch;
    uint32_t commit;
    const char* string;
};

class INucleus : virtual public IRef {
  public:
    virtual IFactory* get_factory() noexcept = 0;
    virtual ILogger* get_logger() noexcept = 0;

    virtual ITable* get_enzyme_finders() noexcept = 0;
    virtual ITable* get_ribosomes() noexcept = 0;

    virtual void synthesize_enzymes() noexcept = 0;
    virtual ITable* get_enzymes() noexcept = 0;

    virtual void register_filter(const IFilter* in, ISubstrate** out) noexcept = 0;

    virtual void set_config(NucleusConfig config) noexcept = 0;
    virtual NucleusConfig get_config() const noexcept = 0;

    virtual void react() noexcept = 0;
    virtual bool is_reacting() const noexcept = 0;

    virtual void create_output(ISubstrate* substrate, IOutput** output) noexcept = 0;
};

CAT_API void create_nucleus(INucleus** out) noexcept;
CAT_API Version get_version() noexcept;

} // namespace catsyn
