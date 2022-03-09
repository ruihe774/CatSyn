#include <stdexcept>

#include <catimpl.h>

void Nucleus::clone(IObject** out) const noexcept {
    not_implemented();
}

IFactory* Nucleus::get_factory() noexcept {
    return this;
}

const ILogger* Nucleus::get_logger() const noexcept {
    return &logger;
}

CAT_API void catsyn::create_nucleus(INucleus** out) {
    *out = new Nucleus;
    (*out)->add_ref();
}
