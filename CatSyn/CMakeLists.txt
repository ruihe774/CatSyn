cmake_minimum_required(VERSION 3.21)

project(CatSyn
    LANGUAGES CXX
    VERSION 1.1.0
    DESCRIPTION "Catalyzed video processing framework"
)

add_library(allostery_lib STATIC
    src/allostery/allostery.cpp
)
target_sources(allostery_lib PUBLIC
    src/allostery/allostery.h
)
target_include_directories(allostery_lib PUBLIC src/allostery)
target_link_libraries(allostery_lib PRIVATE snmalloc_lib)
add_library(allostery SHARED
    src/allostery/allostery.cpp
    src/allostery/allostery.h
)
target_include_directories(allostery PRIVATE src/allostery)
target_link_libraries(allostery PRIVATE snmalloc_lib)
target_link_libraries(allostery INTERFACE allostery_lib)
target_compile_definitions(allostery PRIVATE ALLOSTERY_IMPL)

add_library(tatabox INTERFACE)
target_sources(tatabox PUBLIC
    src/tatabox/tatabox.h
    src/tatabox/pal/windows.h
    src/tatabox/pal/posix.h
)
target_include_directories(tatabox INTERFACE src/tatabox)
target_link_libraries(tatabox INTERFACE fmt)

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
target_link_libraries(catsyn PRIVATE tatabox)
target_link_libraries(catsyn PRIVATE Boost::container)

configure_file(src/catcfg.h.in catcfg.h)

install(TARGETS catsyn allostery)
install(FILES
    include/catsyn.h
    include/cathelper.h
    TYPE INCLUDE
)
install_pdb(catsyn)
install_pdb(allostery)
