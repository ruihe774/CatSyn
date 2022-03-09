#include <stdexcept>

#include <catimpl.h>

void Nucleus::clone(IObject** out) const noexcept {
    not_implemented();
}

CAT_API void create_nucleus(INucleus** out) {
    *out = new Nucleus;
    (*out)->add_ref();
}
