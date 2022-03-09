cmake_minimum_required(VERSION 3.21)

project(Nekomonogatari
    LANGUAGES C CXX
    VERSION 1.0.0
    DESCRIPTION "Monorepo for CatXXX"
)

if(NOT CMAKE_C_COMPILER_ID STREQUAL CMAKE_CXX_COMPILER_ID)
    message(FATAL_ERROR "C compiler and C++ compiler not the same")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only 64bit is supported")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CLANG TRUE)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(GNULIKE TRUE)
endif()

if(WIN32)
    if(MINGW OR CYGWIN)
        message(FATAL_ERROR "MinGW or Cygwin is not supported")
    endif()
    if(NOT CLANG AND NOT MSVC_TOOLSET_VERSION MATCHES "^14.$")
        message(FATAL_ERROR "Only MSVC v14x toolchain is supported")
    endif()
    if(NOT CLANG)
        message(WARNING "Clang is recommended")
    endif()
endif()

set(CAT_OPT_FOR_ARCH "skylake" CACHE STRING "Specify architecture to optimize for")

if(GNULIKE AND CAT_OPT_FOR_ARCH)
    set(OPT_FLAGS "${OPT_FLAGS} -march=${CAT_OPT_FOR_ARCH}")
elseif(MSVC)
    set(OPT_FLAGS "${OPT_FLAGS} /arch:AVX2")
endif()

if(GNULIKE)
    set(OPT_FLAGS "${OPT_FLAGS} -Ofast")
elseif(MSVC)
    set(OPT_FLAGS "${OPT_FLAGS} /fp:fast")
endif()

if(CLANG)
    set(OPT_FLAGS "${OPT_FLAGS} -flto=thin")
elseif(GNULIKE)
    set(OPT_FLAGS "${OPT_FLAGS} -flto")
endif()

if(GNULIKE)
    set(COMPILE_FLAGS "${COMPILE_FLAGS} -fvisibility=hidden -Wall")
endif()
if(MSVC)
    set(COMPILE_FLAGS "${COMPILE_FLAGS} /permissive- /utf-8 /W2 /GS-")
endif()
if(WIN32)
    add_definitions(-D_UNICODE -DUNICODE)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)
    if(GNULIKE)
        set(COMPILE_FLAGS "${COMPILE_FLAGS} -Wno-macro-redefined")
    else()
        set(COMPILE_FLAGS "${COMPILE_FLAGS} /wd4005")
    endif()
endif()

set(CMAKE_CONFIGURATION_TYPES "Release;Debug")
set(CMAKE_CXX_STANDARD 17 REQUIRED)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${COMPILE_FLAGS} ${OPT_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${COMPILE_FLAGS} ${OPT_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILE_FLAGS}")

set(MI_OVERRIDE OFF CACHE BOOL "Override the standard malloc interface (e.g. define entry points for malloc() etc)" FORCE)
set(MI_SKIP_COLLECT_ON_EXIT ON CACHE BOOL "Skip collecting memory on program exit" FORCE)
set(MI_USE_CXX ON CACHE BOOL "Use the C++ compiler to compile the library (instead of the C compiler)" FORCE)
add_subdirectory(mimalloc EXCLUDE_FROM_ALL)

add_subdirectory(boost EXCLUDE_FROM_ALL)

add_subdirectory(CatSyn)

if(CAT_BUILD_TESTS)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()
