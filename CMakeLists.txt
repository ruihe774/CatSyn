cmake_minimum_required(VERSION 3.21)

project(Nekomonogatari
    LANGUAGES C CXX
    VERSION 1.0.0
    DESCRIPTION "Monorepo for CatXXX"
)

include(config.cmake)
include(retrieve_commit.cmake)
include(install_pdb.cmake)

include(mimalloc.cmake)

add_subdirectory(boost EXCLUDE_FROM_ALL)

add_subdirectory(fmt EXCLUDE_FROM_ALL)

add_subdirectory(CatSyn)

add_subdirectory(Metalloporphyrin)

if(CAT_BUILD_TESTS)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()
