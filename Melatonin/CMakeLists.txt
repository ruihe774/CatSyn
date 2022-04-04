cmake_minimum_required(VERSION 3.21)

project(Melatonin
    LANGUAGES CXX
    VERSION 1.0.0
    DESCRIPTION "Lua binding for CatSyn"
)

add_library(melatonin SHARED
    src/melatonin.cpp
)
target_link_libraries(melatonin PRIVATE luajit)
target_link_libraries(melatonin PRIVATE catsyn)
target_link_libraries(melatonin PRIVATE tatabox)
