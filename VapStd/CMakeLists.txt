cmake_minimum_required(VERSION 3.21)

project(VapStd
    LANGUAGES C CXX
    VERSION 54.0.0
    DESCRIPTION "Standalone VapourSynth core filters"
)

include_directories(.)

add_library(vapstd SHARED
    boxblurfilter.cpp
    cpufeatures.cpp
    exprfilter.cpp
    genericfilters.cpp
    kernel/cpulevel.cpp
    kernel/generic.cpp
    kernel/merge.c
    kernel/planestats.c
    kernel/transpose.c
    kernel/x86/generic_sse2.cpp
    kernel/x86/merge_sse2.c
    kernel/x86/planestats_sse2.c
    kernel/x86/transpose_sse2.c
    kernel/x86/generic_avx2.cpp
    kernel/x86/merge_avx2.c
    kernel/x86/planestats_avx2.c
    lutfilters.cpp
    mergefilters.c
    reorderfilters.c
    simplefilters.c
    textfilter.cpp
    vsresize.cpp
    VapourSynth.h
    VSHelper.h
    cpufeatures.h
    filtershared.h
    filtersharedcpp.h
    internalfilters.h
    jitasm.h
    kernel/cpulevel.h
    kernel/generic.h
    kernel/merge.h
    kernel/planestats.h
    kernel/transpose.h
    ter-116n.h
    x86utils.h
    p2p.h
)

target_link_libraries(vapstd PRIVATE zimg)
target_compile_definitions(vapstd PRIVATE VS_CORE_EXPORTS VS_TARGET_OS_WINDOWS VS_TARGET_CPU_X86 _CRT_SECURE_NO_WARNINGS)

add_library(vsstd SHARED VSStd.cpp internalfilters.h)
target_link_libraries(vsstd PRIVATE vapstd)

add_library(vsresize SHARED VSRez.cpp internalfilters.h)
target_link_libraries(vsresize PRIVATE vapstd)

add_library(vstext SHARED VSText.cpp internalfilters.h)
target_link_libraries(vstext PRIVATE vapstd)

install(TARGETS vapstd vsstd vsresize vstext)
