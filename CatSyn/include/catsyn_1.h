#pragma once

#include <catsyn.h>

namespace catsyn {

class IPathway : virtual public IObject {
  public:
    virtual void add_step(const char* enzyme_id, const char* func_name, const ITable* args, ISubstrate** out) = 0;
};

class IFactory1 : virtual public IFactory {
  public:
    virtual void create_pathway(IPathway** out) noexcept = 0;
};

class IFilter1 : virtual public IFilter {
  public:
    virtual std::atomic_uint* get_thread_init_atomic() noexcept = 0;
};

} // namespace catsyn
