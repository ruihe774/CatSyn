cmake_minimum_required(VERSION 3.21)

project(Valve
    LANGUAGES C
    VERSION 1.0.0
    DESCRIPTION "Larger pipe"
)

add_executable(valve valve.c)
install(TARGETS valve)
