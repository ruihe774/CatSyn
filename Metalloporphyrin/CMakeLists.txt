cmake_minimum_required(VERSION 3.21)

project(Metalloporphyrin
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "VapourSynth compatibility layer for CatSyn"
)

add_library(metalloporphyrin SHARED
    include/VapourSynth.h
    src/porphyrin.h
    src/api.cpp
    src/core.cpp
    src/frame.cpp
    src/map.cpp
    src/message.cpp
    ../CatSyn/src/new-delete.cpp
)
target_include_directories(metalloporphyrin PUBLIC include)
target_include_directories(metalloporphyrin PRIVATE src)
target_include_directories(metalloporphyrin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(metalloporphyrin PRIVATE mimalloc)
target_link_libraries(metalloporphyrin PRIVATE fmt)
target_link_libraries(metalloporphyrin PRIVATE Boost::container)
target_link_libraries(metalloporphyrin PRIVATE catsyn)

configure_file(src/metalcfg.h.in metalcfg.h)
