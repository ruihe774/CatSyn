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

} // namespace catsyn
