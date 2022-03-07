#pragma once

#include <atomic>

#define CAT_IMPL

#include <catsyn.h>
#include <cathelper.h>

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

class Nucleus final : public Object, public INucleus {
    AllocStat alloc_stat;

  public:
    void create_bytes(const void* data, size_t len, IBytes** out) noexcept final;
    void create_aligned_bytes(const void* data, size_t len, IAlignedBytes** out) noexcept final;
};
