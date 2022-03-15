cmake_minimum_required(VERSION 3.21)

project(Metalloporphyrin
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "VapourSynth compatibility layer for CatSyn")

add_library(metalloporphyrin SHARED
    include/VapourSynth.h
    src/metalloporphyrin.cpp
)
target_compile_definitions(metalloporphyrin PRIVATE VS_CORE_EXPORTS)
target_include_directories(metalloporphyrin PUBLIC include)
target_link_libraries(metalloporphyrin PRIVATE catsyn)
target_link_libraries(metalloporphyrin PRIVATE Boost::container)
