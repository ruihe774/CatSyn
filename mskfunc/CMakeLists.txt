cmake_minimum_required(VERSION 3.21)

project(mskfunc
    LANGUAGES C CXX
    VERSION 1.2.4
    DESCRIPTION "A typography and shape library for Aegisub, based on DirectWrite and Direct2D"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/transform_lib.h"
    COMMAND moonjit -b "${CMAKE_CURRENT_SOURCE_DIR}/transform_lib.lua" "${CMAKE_CURRENT_BINARY_DIR}/transform_lib.h"
    WORKING_DIRECTORY "${NEKO_ROOT}/moonjit/src"
    DEPENDS transform_lib.lua
)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run_text_template.h"
    COMMAND moonjit -b "${CMAKE_CURRENT_SOURCE_DIR}/run_text_template.lua" "${CMAKE_CURRENT_BINARY_DIR}/run_text_template.h"
    WORKING_DIRECTORY "${NEKO_ROOT}/moonjit/src"
    DEPENDS run_text_template.lua
)
add_library(mskfunc SHARED
    mskfunc.cpp
    lowway.h
    "${CMAKE_CURRENT_BINARY_DIR}/transform_lib.h"
    "${CMAKE_CURRENT_BINARY_DIR}/run_text_template.h"
)
target_include_directories(mskfunc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mskfunc PRIVATE dwrite d2d1 d3d11 Shlwapi luajit)
target_link_libraries(mskfunc PRIVATE Boost::container)
target_link_libraries(mskfunc PRIVATE WIL)
