cmake_minimum_required(VERSION 3.21)

project(Valve
    LANGUAGES C
    VERSION 1.0.0
    DESCRIPTION "Larger pipe"
)

set(CMAKE_C_STANDARD 11 REQUIRED)

if(MSVC)
    add_compile_options(/wd5105)
    add_link_options(/STACK:35000000)
elseif(WIN32 AND CLANG)
    add_link_options(-Wl,/STACK:35000000)
elseif(GNULIKE)
    add_link_options(-Wl,--stack,35000000)
endif()

add_executable(valve valve.c)
install(TARGETS valve)
