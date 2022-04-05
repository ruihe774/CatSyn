cmake_minimum_required(VERSION 3.21)

project(Nekomonogatari
    LANGUAGES C CXX
    VERSION 1.0.0
    DESCRIPTION "Monorepo for catalysts"
    HOMEPAGE_URL https://github.com/Yusyabu/Nekomonogatari
)

include(config.cmake)
include(retrieve_commit.cmake)
include(install_pdb.cmake)

set(NEKO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

set(C_FLAGS_BACKUP "${CMAKE_C_FLAGS}")
set(CXX_FLAGS_BACKUP "${CMAKE_CXX_FLAGS}")

if(CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Xclang -fexternc-nounwind")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xclang -fexternc-nounwind")
endif()

add_subdirectory(snmalloc EXCLUDE_FROM_ALL)
target_compile_definitions(snmalloc_lib INTERFACE SNMALLOC_USE_LARGE_CHUNKS)
set(MI_USE_CXX ON CACHE BOOL "Use the C++ compiler to compile the library (instead of the C compiler)" FORCE)
add_subdirectory(mimalloc EXCLUDE_FROM_ALL)

add_subdirectory(boost EXCLUDE_FROM_ALL)

add_subdirectory(fmt EXCLUDE_FROM_ALL)

set(WIL_BUILD_PACKAGING OFF CACHE BOOL "")
set(WIL_BUILD_TESTS OFF CACHE BOOL "")
add_subdirectory(wil EXCLUDE_FROM_ALL)

add_subdirectory(zimg EXCLUDE_FROM_ALL)

add_subdirectory(CatSyn)

add_subdirectory(Metalloporphyrin)

add_subdirectory(VapStd)

set(CMAKE_C_FLAGS "${C_FLAGS_BACKUP}")
set(CMAKE_CXX_FLAGS "${CXX_FLAGS_BACKUP}")

set(LUAJIT_ENABLE_LUA52COMPAT ON CACHE BOOL "Enable Lua 5.2 features that might break")
set(LUAJIT_DISABLE_JIT ON CACHE BOOL "Disable the JIT compiler")
add_subdirectory(moonjit)

add_subdirectory(Melatonin)

add_subdirectory(mskfunc)
