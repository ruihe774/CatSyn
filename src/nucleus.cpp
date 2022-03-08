#include <stdexcept>

#include <catimpl.h>

[[noreturn]] static void not_implemented() {
    throw std::logic_error("not implemented");
}

void Nucleus::clone(IObject** out) const noexcept {
    not_implemented();
}
