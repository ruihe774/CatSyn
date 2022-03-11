cmake_minimum_required(VERSION 3.21)

project(CatSyn
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "Catalyzed video processing framework"
)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4250")
endif()

add_library(boost_sync INTERFACE)
add_library(Boost::sync ALIAS boost_sync)
target_include_directories(boost_sync INTERFACE ../boost/libs/sync/include)

if(WIN32)
    add_library(Boost::stacktrace ALIAS boost_stacktrace_windbg)
else()
    add_library(Boost::stacktrace ALIAS boost_stacktrace_backtrace)
endif()

add_library(catsyn SHARED
    include/cathelper.h
    include/catsyn.h
    src/catimpl.h
    src/enzyme.cpp
    src/frame.cpp
    src/logger.cpp
    src/new-delete-override.cpp
    src/nucleus.cpp
    src/table.cpp
    src/threading.cpp)
target_include_directories(catsyn PUBLIC include)
target_include_directories(catsyn PRIVATE src)
target_link_libraries(catsyn PRIVATE mimalloc-static)
target_link_libraries(catsyn PRIVATE
    Boost::algorithm
    Boost::dll
    Boost::thread
    Boost::sync
    Boost::lockfree
    Boost::stacktrace)
target_compile_definitions(catsyn PRIVATE BOOST_STACKTRACE_LINK)

if(NOT WIN32)
    target_link_libraries(catsyn PRIVATE dl backtrace)
endif()
