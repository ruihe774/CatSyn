#pragma once

#include <atomic>

#include <boost/lockfree/queue.hpp>
#include <boost/sync/semaphore.hpp>
#include <boost/thread/thread.hpp>

#define CAT_IMPL

#include <cathelper.h>
#include <catsyn.h>

using namespace catsyn;

class Object : virtual public catsyn::IObject {
  private:
    void drop() noexcept final {
        delete this;
    }
};

class AllocStat {
    std::atomic_size_t current{0};

  public:
    void alloc(size_t size) noexcept;
    void free(size_t size) noexcept;

    size_t get_current() const noexcept;
};

void write_err(const char* s, size_t n) noexcept;

void thread_init() noexcept;

class Thread final : public boost::thread {
    template<typename F, typename... Args> static void proxy(F f, Args... args) {
        thread_init();
        f(std::forward<Args>(args)...);
    }

    template<typename T, bool c> struct unbox_reference_if {};
    template<typename T> struct unbox_reference_if<T, true> {
        typedef typename boost::unwrap_reference<T>::type& type;
    };
    template<typename T> struct unbox_reference_if<T, false> { typedef T type; };
    template<typename T>
    using unbox_reference_t = typename unbox_reference_if<T, boost::is_reference_wrapper<T>::value>::type;

  public:
    template<typename F, typename... Args>
    explicit Thread(F f, Args&&... args)
        : boost::thread(proxy<F, unbox_reference_t<Args>...>, f, std::forward<Args>(args)...) {}
};

class Logger final : public Object, public ILogger {
    mutable boost::lockfree::queue<uintptr_t, boost::lockfree::capacity<128>> queue;
    mutable boost::sync::semaphore semaphore;
    cat_ptr<ILogSink> sink;
    LogLevel filter_level;
    Thread thread;

  public:
    Logger();
    void log(LogLevel level, const char* msg) const noexcept final;
    void set_level(LogLevel level) noexcept final;
    void set_sink(ILogSink* in) noexcept final;
    void clone(IObject** out) const noexcept final;
    ~Logger() final;
};

class Nucleus final : public Object, public INucleus, public IFactory {
  public:
    AllocStat alloc_stat;
    Logger logger;

    void calling_thread_init() noexcept final;

    IFactory* get_factory() noexcept final;
    ILogger* get_logger() noexcept final;

    void create_bytes(const void* data, size_t len, IBytes** out) noexcept final;
    void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept final;
    void create_number_array(SampleType sample_type, const void* data, size_t len, INumberArray** out) noexcept final;

    void create_frame(FrameInfo fi, const IAlignedBytes** planes, const size_t* strides, const ITable* props,
                      IFrame** out) noexcept final;
    void create_table(size_t reserve_capacity, ITable** out) noexcept final;

    void clone(IObject** out) const noexcept final;
};

class Shuttle {
  protected:
    Nucleus& nucl;
    explicit Shuttle(Nucleus& nucl) : nucl(nucl) {}
};

class NotImplemted : public std::logic_error {
  public:
    NotImplemted() : std::logic_error("not implemented") {}
};

[[noreturn]] inline void not_implemented() {
    throw NotImplemted();
}
