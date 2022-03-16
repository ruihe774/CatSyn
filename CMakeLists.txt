cmake_minimum_required(VERSION 3.21)

project(Nekomonogatari
    LANGUAGES C CXX
    VERSION 1.0.0
    DESCRIPTION "Monorepo for CatXXX"
)

include(config.cmake)
include(retrieve_commit.cmake)

set(MI_OVERRIDE OFF CACHE BOOL "Override the standard malloc interface (e.g. define entry points for malloc() etc)" FORCE)
set(MI_USE_CXX ON CACHE BOOL "Use the C++ compiler to compile the library (instead of the C compiler)" FORCE)
add_subdirectory(mimalloc EXCLUDE_FROM_ALL)

add_subdirectory(boost EXCLUDE_FROM_ALL)

add_subdirectory(fmt EXCLUDE_FROM_ALL)

add_subdirectory(CatSyn)

add_subdirectory(Metalloporphyrin)

if(CAT_BUILD_TESTS)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()
