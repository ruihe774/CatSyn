cmake_minimum_required(VERSION 3.21)
project(CatSyn)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive- /utf-8 /W3 /D_UNICODE /DUNICODE /GS-")

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

add_library(catsyn SHARED
    include/cathelper.h
    include/catsyn.h
    src/bytes.cpp
    src/catimpl.h
)
target_include_directories(catsyn PUBLIC include)
target_include_directories(catsyn PRIVATE src)
