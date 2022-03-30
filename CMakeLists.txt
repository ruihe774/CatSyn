cmake_minimum_required(VERSION 3.21)

project(Nekomonogatari
    LANGUAGES C CXX
    VERSION 1.0.0
    DESCRIPTION "Monorepo for catalysts"
)

include(config.cmake)
include(retrieve_commit.cmake)
include(install_pdb.cmake)

add_subdirectory(snmalloc EXCLUDE_FROM_ALL)
target_compile_definitions(snmalloc_lib INTERFACE SNMALLOC_USE_LARGE_CHUNKS)

add_subdirectory(boost EXCLUDE_FROM_ALL)

add_subdirectory(fmt EXCLUDE_FROM_ALL)

add_subdirectory(CatSyn)

add_subdirectory(Metalloporphyrin)

add_subdirectory(zimg EXCLUDE_FROM_ALL)

add_subdirectory(VapStd)
