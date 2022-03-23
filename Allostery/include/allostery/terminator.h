#pragma once

#include <exception>

namespace allostery {

struct StopRequested : std::exception {
    //    StopRequested() noexcept : std::exception() {}
};

}
