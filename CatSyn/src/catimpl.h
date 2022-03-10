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

class Logger final : public Object, public ILogger {
    mutable boost::lockfree::queue<uintptr_t, boost::lockfree::capacity<128>> queue;
    mutable boost::sync::semaphore semaphore;
    cat_ptr<ILogSink> sink;
    LogLevel filter_level;
    boost::thread thread;

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

    IFactory* get_factory() noexcept final;
    ILogger* get_logger() noexcept final;

    void create_bytes(const void* data, size_t len, IBytes** out) noexcept final;
    void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept final;

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
