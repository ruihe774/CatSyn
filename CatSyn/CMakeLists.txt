cmake_minimum_required(VERSION 3.21)

project(CatSyn
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "Catalyzed video processing framework"
)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4250")
endif()

add_library(catsyn SHARED
    include/cathelper.h
    include/catsyn.h
    src/catimpl.h
    src/frame.cpp
    src/logger.cpp
    src/new-delete-override.cpp
    src/nucleus.cpp
    src/table.cpp)
target_include_directories(catsyn PUBLIC include)
target_include_directories(catsyn PRIVATE src)
target_link_libraries(catsyn PRIVATE mimalloc-static)
