project(Allostery
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "Lock-free allocator and data structures"
)

add_library(allostery STATIC
    src/allocator.cpp
)
target_link_libraries(allostery PRIVATE snmalloc_lib)
