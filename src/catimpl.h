#pragma once

#include <catsyn.h>

class CatImpl : public catsyn::Object {
  private:
    void drop() noexcept final {
        delete this;
    }
};
