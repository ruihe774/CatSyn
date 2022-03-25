cmake_minimum_required(VERSION 3.21)

project(CatSyn
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "Catalyzed video processing framework"
)

add_library(allostery STATIC
    src/allostery.cpp
)
target_sources(allostery PUBLIC
    src/allostery.h
)
target_include_directories(allostery PUBLIC src)
target_link_libraries(allostery PRIVATE snmalloc_lib)

add_library(catsyn SHARED
    src/catimpl.h
    src/queue.h
    src/enzyme.cpp
    src/frame.cpp
    src/logger.cpp
    src/nucleus.cpp
    src/substrate.cpp
    src/table.cpp
)
target_sources(catsyn PUBLIC
    include/cathelper.h
    include/catsyn.h
)
target_include_directories(catsyn PUBLIC include)
target_include_directories(catsyn PRIVATE src)
target_include_directories(catsyn PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(catsyn PRIVATE allostery)
target_link_libraries(catsyn PRIVATE fmt)
target_link_libraries(catsyn PRIVATE
    Boost::container
    Boost::dll
)

configure_file(src/catcfg.h.in catcfg.h)

install(TARGETS catsyn)
install(FILES
    include/catsyn.h
    include/cathelper.h
    TYPE INCLUDE
)
install_pdb(catsyn)
