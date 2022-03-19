cmake_minimum_required(VERSION 3.21)

project(CatSyn
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "Catalyzed video processing framework"
)

if(WIN32)
    add_library(Boost::stacktrace ALIAS boost_stacktrace_windbg)
else()
    add_library(Boost::stacktrace ALIAS boost_stacktrace_backtrace)
endif()

add_library(catsyn SHARED
    src/catimpl.h
    src/enzyme.cpp
    src/frame.cpp
    src/logger.cpp
    src/new-delete.cpp
    src/nucleus.cpp
    src/substrate.cpp
    src/table.cpp
    src/threading.cpp
)
target_sources(catsyn PUBLIC
    include/cathelper.h
    include/catsyn.h
)
target_include_directories(catsyn PUBLIC include)
target_include_directories(catsyn PRIVATE src)
target_include_directories(catsyn PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(catsyn PRIVATE mimalloc snmalloc_lib)
target_link_libraries(catsyn PRIVATE fmt)
target_link_libraries(catsyn PRIVATE
    Boost::algorithm
    Boost::container
    Boost::dll
    Boost::lockfree
    Boost::stacktrace
)
target_compile_definitions(catsyn PRIVATE BOOST_STACKTRACE_LINK)

if(NOT WIN32)
    target_link_libraries(catsyn PRIVATE dl backtrace)
endif()

configure_file(src/catcfg.h.in catcfg.h)

install(TARGETS catsyn)
install(FILES
    include/catsyn.h
    include/cathelper.h
    TYPE INCLUDE
)
install_pdb(catsyn)
