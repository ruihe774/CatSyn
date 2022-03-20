#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
#include <semaphore>

#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/lockfree/queue.hpp>

#include <fmt/format.h>

#define CAT_IMPL

#include <catcfg.h>
#include <cathelper.h>
#include <catsyn.h>

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

void write_err(const char* s, size_t n) noexcept;

void thread_init() noexcept;

class Thread final : public std::thread {
    template<typename F, typename... Args>
    static void proxy(F f, Args... args) noexcept(noexcept(std::invoke(f, std::forward<Args>(args)...))) {
        thread_init();
        std::invoke(f, std::forward<Args>(args)...);
    }

    template<typename T> struct unbox_reference { typedef T type; };
    template<typename U> struct unbox_reference<std::reference_wrapper<U>> {
        typedef std::add_lvalue_reference_t<U> type;
    };
    template<typename T> using unbox_reference_t = typename unbox_reference<T>::type;

  public:
    template<typename F, typename... Args>
    explicit Thread(F f, Args&&... args)
        : std::thread(proxy<F, unbox_reference_t<Args>...>, f, std::forward<Args>(args)...) {}
};

using Semaphore = std::counting_semaphore<std::numeric_limits<int>::max()>;
using BinarySemaphore = std::binary_semaphore;

class Logger final : public Object, public ILogger {
    mutable boost::lockfree::queue<uintptr_t, boost::lockfree::capacity<128>> queue;
    mutable BinarySemaphore semaphore;
    cat_ptr<ILogSink> sink;
    LogLevel filter_level;
    std::atomic_bool stop;
    Thread thread;

  public:
    Logger();
    void log(LogLevel level, const char* msg) const noexcept final;
    void set_level(LogLevel level) noexcept final;
    void set_sink(ILogSink* in) noexcept final;
    void clone(IObject** out) const noexcept final;
    ~Logger() final;
};

class Table final : public Object, public ITable {
    typedef boost::container::small_vector<std::pair<std::optional<std::string>, cat_ptr<const IObject>>, 16>
        vector_type;
    vector_type vec;

    size_t norm_ref(size_t ref) const noexcept;
    void expand(size_t len) noexcept;

  public:
    explicit Table(vector_type vec) noexcept;
    explicit Table(size_t reserve_capacity) noexcept;
    size_t size() const noexcept final;
    void resize(size_t len) noexcept final;
    const IObject* get(size_t ref) const noexcept final;
    void set(size_t ref, const IObject* obj) noexcept final;
    size_t get_ref(const char* key) const noexcept final;
    const char* get_key(size_t ref) const noexcept final;
    void set_key(size_t ref, const char* key) noexcept final;
    void clone(IObject** out) const noexcept final;
};

class Substrate final : public Object, public ISubstrate, public Shuttle {
  public:
    boost::container::small_flat_map<std::thread::id, cat_ptr<IFilter>, 64> filters;

    VideoInfo get_video_info() const noexcept final;
    INucleus* get_nucleus() noexcept final;

    Substrate(Nucleus& nucl, cat_ptr<const IFilter> filter) noexcept;
};

struct FrameInstance;

class MaintainTask {
  public:
    static constexpr size_t payload_size = sizeof(std::exception_ptr);

  private:
    uintptr_t p;
    size_t v;
    std::array<std::byte, payload_size> pl;

  public:
    enum class Type {
        Construct,
        Notify,
    };

    void* get_pointer() const noexcept;
    size_t get_value() const noexcept;
    Type get_type() const noexcept;
    std::array<std::byte, payload_size> get_payload() const noexcept;

    static MaintainTask create(Type, void*, size_t, std::array<std::byte, payload_size> = {}) noexcept;
};

struct CallbackTask {
    IOutput::Callback* callback;
    const IFrame* frame;
    std::array<std::byte, MaintainTask::payload_size> exc;
};

NucleusConfig get_default_config() noexcept;

class Nucleus final : public Object, public INucleus, public IFactory {
  public:
    struct Accountant {
        size_t mem{0};
        size_t bubble{0};
        ~Accountant();
    };

    Logger logger;
    Accountant accountant;
    NucleusConfig config{get_default_config()};

    TableView<Table> finders{nullptr};
    TableView<Table> ribosomes{nullptr};
    TableView<Table> enzymes{nullptr};

    std::atomic_bool stop{false};
    std::vector<Thread> worker_threads;
    std::optional<Thread> maintainer_thread;
    std::optional<Thread> callback_thread;
    Semaphore work_semaphore{0};
    boost::lockfree::queue<FrameInstance*> work_queue{128};
    BinarySemaphore maintain_semaphore{0};
    boost::lockfree::queue<MaintainTask> maintain_queue{128};
    BinarySemaphore callback_semaphore{0};
    boost::lockfree::queue<CallbackTask> callback_queue{128};

    Nucleus();
    ~Nucleus() final;

    void calling_thread_init() noexcept final;

    IFactory* get_factory() noexcept final;
    ILogger* get_logger() noexcept final;

    ITable* get_enzyme_finders() noexcept final;
    ITable* get_ribosomes() noexcept final;

    void create_bytes(const void* data, size_t len, IBytes** out) noexcept final;
    void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept final;
    void create_number_array(SampleType sample_type, const void* data, size_t len, INumberArray** out) noexcept final;

    void create_frame(FrameInfo fi, const IAlignedBytes** planes, const size_t* strides, const ITable* props,
                      IFrame** out) noexcept final;
    void create_table(size_t reserve_capacity, ITable** out) noexcept final;

    void create_dll_enzyme_finder(const char* path, IEnzymeFinder** out) noexcept final;
    void create_catsyn_v1_ribosome(IRibosome** out) noexcept final;

    void synthesize_enzymes() noexcept final;
    ITable* get_enzymes() noexcept final;

    void register_filter(const IFilter* in, ISubstrate** out) noexcept final;

    void set_config(NucleusConfig config) noexcept final;
    NucleusConfig get_config() const noexcept final;

    void react() noexcept final;
    bool is_reacting() const noexcept final;

    void create_output(ISubstrate* substrate, IOutput** output) noexcept final;
};

class NotImplemted : public std::logic_error {
  public:
    NotImplemted() : std::logic_error("not implemented") {}
};

[[noreturn]] inline void not_implemented() {
    throw NotImplemted();
}

[[noreturn]] inline void insufficient_buffer() {
    throw std::runtime_error("insufficient buffer");
}

[[noreturn]] void throw_system_error();

template<typename... Args> const char* format_c(fmt::format_string<Args...> fmt, Args&&... args) noexcept {
    thread_local char buf[4096];
    auto size = fmt::format_to_n(buf, sizeof(buf) - 1, std::move(fmt), std::forward<Args>(args)...).size;
    if (size >= sizeof(buf))
        insufficient_buffer();
    buf[size] = 0;
    return buf;
}

template<typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<std::is_base_of_v<std::exception, T>>> : fmt::formatter<const char*> {
    template<typename FormatContext> constexpr auto format(const std::exception& exc, FormatContext& ctx) const {
        return fmt::formatter<const char*>::format(exc.what(), ctx);
    }
};

void set_thread_priority(std::thread& thread, int priority, bool allow_boost = true) noexcept;
