#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <boost/container/small_vector.hpp>

#include <tatabox.h>

#define CAT_IMPL

#include <catcfg.h>
#include <cathelper.h>
#include <catsyn_1.h>

#include <queue.h>

using namespace catsyn;

class Object : virtual public IObject {
  private:
    void drop() noexcept final {
        delete this;
    }
};

class Nucleus;

class Shuttle {
  protected:
    Nucleus& nucl;
    explicit Shuttle(Nucleus& nucl) noexcept;
};

class JThread final : public std::jthread {
    template<typename F, typename... Args>
    static void proxy(F f, Args... args) noexcept(noexcept(std::invoke(f, std::forward<Args>(args)...))) {
        try {
            std::invoke(f, std::forward<Args>(args)...);
        } catch (StopRequested&) {
        }
    }

    template<typename T> struct unbox_reference { typedef T type; };
    template<typename U> struct unbox_reference<std::reference_wrapper<U>> {
        typedef std::add_lvalue_reference_t<U> type;
    };
    template<typename T> using unbox_reference_t = typename unbox_reference<T>::type;

  public:
    template<typename F, typename... Args>
    explicit JThread(F f, Args&&... args)
        : std::jthread(proxy<F, unbox_reference_t<Args>...>, f, std::forward<Args>(args)...) {}
};

class Logger final : public Object, virtual public ILogger {
    mutable SCQueue<uintptr_t> queue;
    cat_ptr<ILogSink> sink;
    LogLevel filter_level;
    JThread thread;

  public:
    Logger();
    void log(LogLevel level, const char* msg) const noexcept final;
    void set_level(LogLevel level) noexcept final;
    void set_sink(ILogSink* in) noexcept final;
    void clone(IObject** out) const noexcept final;
    ~Logger() final;
};

class Table final : public Object, public virtual ITable {
    typedef boost::container::small_vector<std::pair<std::optional<std::string>, cat_ptr<const IObject>>, 16>
        vector_type;
    vector_type vec;

  public:
    using ITable::npos;
    explicit Table(const Table& other) noexcept;
    explicit Table(size_t reserve_capacity) noexcept;
    void clone(IObject** out) const noexcept final;
    const IObject* get(size_t ref, const char** key_out) const noexcept final;
    void set(size_t ref, const IObject* obj, const char* key) noexcept final;
    size_t erase(size_t ref) noexcept final;
    size_t find(const char* key) const noexcept final;
    size_t size() const noexcept final;
    void clear() noexcept final;
    size_t next(size_t ref) const noexcept final;
    size_t prev(size_t ref) const noexcept final;
};

class Substrate final : public Object, virtual public ISubstrate {
  public:
    cat_ptr<IFilter> filter;

    VideoInfo get_video_info() const noexcept final;

    Substrate(Nucleus& nucl, cat_ptr<const IFilter> filter) noexcept;
};

struct FrameInstance;
struct FrameInstanceTickGreater {
    bool operator()(const FrameInstance* l, const FrameInstance* r) const noexcept;
};

typedef std::function<void(const IFrame* frame, std::exception_ptr exc)> InnerCallback;

struct Construct {
    cat_ptr<Substrate> substrate;
    size_t frame_idx;
    std::unique_ptr<InnerCallback> callback;
};
struct Notify {
    FrameInstance* inst;
    std::exception_ptr exc;
};

struct MaintainTask : std::variant<Construct, Notify> {
    explicit MaintainTask(cat_ptr<Substrate> substrate, size_t frame_idx,
                          std::unique_ptr<InnerCallback> callback = {}) noexcept;
    explicit MaintainTask(FrameInstance* inst, std::exception_ptr exc = {}) noexcept;
};

struct CallbackTask {
    std::function<void()> callback;
};

NucleusConfig create_config(NucleusConfig tmpl = {}) noexcept;

class Nucleus final : public Object, virtual public INucleus, virtual public IFactory1 {
  public:
    NucleusConfig config{create_config()};

    Logger logger;

    cat_ptr<Table> finders;
    cat_ptr<Table> ribosomes;
    cat_ptr<Table> enzymes;

    std::map<const IFilter*, cat_ptr<ISubstrate>> substrates;

    SCQueue<MaintainTask> maintain_queue;
    SCQueue<CallbackTask> callback_queue;
    PriorityQueue<FrameInstance*, FrameInstanceTickGreater> work_queue;
    std::optional<JThread> maintainer_thread;
    std::optional<JThread> callback_thread;
    std::vector<JThread> worker_threads;

    Nucleus();
    ~Nucleus() final;

    IFactory* get_factory() noexcept final;
    ILogger* get_logger() noexcept final;

    ITable* get_enzyme_finders() noexcept final;
    ITable* get_ribosomes() noexcept final;

    void create_bytes(const void* data, size_t len, IBytes** out) noexcept final;
    void create_numeric(SampleType sample_type, const void* data, size_t bytes_count, INumeric** out) noexcept final;

    void create_frame(FrameInfo fi, const IBytes** planes, const size_t* strides, const ITable* props,
                      IFrame** out) noexcept final;
    void create_table(size_t reserve_capacity, ITable** out) noexcept final;

    void create_dll_enzyme_finder(const char* path, IEnzymeFinder** out) noexcept final;
    void create_catsyn_v1_ribosome(IRibosome** out) noexcept final;
    void create_pathway(IPathway** out) noexcept final;

    void synthesize_enzymes() noexcept final;
    ITable* get_enzymes() noexcept final;

    ISubstrate* register_filter(const IFilter* filter) noexcept final;
    void unregister_filter(const IFilter* filter) noexcept final;

    void set_config(NucleusConfig config) noexcept final;
    NucleusConfig get_config() const noexcept final;

    void react() noexcept final;
    bool is_reacting() const noexcept final;

    void create_output(ISubstrate* substrate, IOutput** output) noexcept final;
};
