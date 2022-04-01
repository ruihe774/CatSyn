#pragma once

#include <catsyn.h>

namespace catsyn {

class IAntagonist : virtual public IRef {};

class IPathway : virtual public IObject {
  public:
    virtual void add_step(const char* enzyme_id, const char* func_name, const ITable* args, IAntagonist** out) = 0;
    virtual void set_slot(int id, const IAntagonist* anta) noexcept = 0;
    virtual IAntagonist* get_slot(int id) const noexcept = 0;
    virtual ISubstrate* make_substrate(const IAntagonist* anta) = 0;
};

class IFactory1 : virtual public IFactory {
  public:
    virtual void create_pathway(IPathway** out) noexcept = 0;
};

} // namespace catsyn
