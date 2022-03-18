cmake_minimum_required(VERSION 3.21)

project(zimg
    LANGUAGES CXX
    VERSION 3.0.3
    DESCRIPTION "Scaling, colorspace conversion, and dithering library"
)

add_library(zimg STATIC)

target_sources(zimg PUBLIC
    zimg/src/zimg/api/zimg.h
    zimg/src/zimg/api/zimg++.hpp
)

target_sources(zimg PRIVATE
    zimg/src/zimg/colorspace/arm/operation_impl_arm.h
    zimg/src/zimg/colorspace/colorspace.h
    zimg/src/zimg/colorspace/colorspace_param.h
    zimg/src/zimg/colorspace/gamma.h
    zimg/src/zimg/colorspace/graph.h
    zimg/src/zimg/colorspace/matrix3.h
    zimg/src/zimg/colorspace/operation.h
    zimg/src/zimg/colorspace/operation_impl.h
    zimg/src/zimg/colorspace/x86/gamma_constants_avx512.h
    zimg/src/zimg/colorspace/x86/operation_impl_x86.h
    zimg/src/zimg/common/align.h
    zimg/src/zimg/common/alloc.h
    zimg/src/zimg/common/arm/cpuinfo_arm.h
    zimg/src/zimg/common/arm/neon_util.h
    zimg/src/zimg/common/builder.h
    zimg/src/zimg/common/checked_int.h
    zimg/src/zimg/common/cpuinfo.h
    zimg/src/zimg/common/except.h
    zimg/src/zimg/common/libm_wrapper.h
    zimg/src/zimg/common/make_array.h
    zimg/src/zimg/common/matrix.h
    zimg/src/zimg/common/ccdep.h
    zimg/src/zimg/common/pixel.h
    zimg/src/zimg/common/static_map.h
    zimg/src/zimg/common/x86/avx2_util.h
    zimg/src/zimg/common/x86/avx512_util.h
    zimg/src/zimg/common/x86/avx_util.h
    zimg/src/zimg/common/x86/cpuinfo_x86.h
    zimg/src/zimg/common/x86/sse2_util.h
    zimg/src/zimg/common/x86/sse_util.h
    zimg/src/zimg/common/x86/x86util.h
    zimg/src/zimg/common/zassert.h
    zimg/src/zimg/depth/arm/depth_convert_arm.h
    zimg/src/zimg/depth/arm/dither_arm.h
    zimg/src/zimg/depth/arm/f16c_arm.h
    zimg/src/zimg/depth/blue.h
    zimg/src/zimg/depth/depth.h
    zimg/src/zimg/depth/depth_convert.h
    zimg/src/zimg/depth/dither.h
    zimg/src/zimg/depth/quantize.h
    zimg/src/zimg/depth/x86/depth_convert_x86.h
    zimg/src/zimg/depth/x86/dither_x86.h
    zimg/src/zimg/depth/x86/f16c_x86.h
    zimg/src/zimg/graph/basic_filter.h
    zimg/src/zimg/graph/filtergraph.h
    zimg/src/zimg/graph/graphnode.h
    zimg/src/zimg/graph/graphbuilder.h
    zimg/src/zimg/graph/image_filter.h
    zimg/src/zimg/graph/image_buffer.h
    zimg/src/zimg/resize/arm/resize_impl_arm.h
    zimg/src/zimg/resize/filter.h
    zimg/src/zimg/resize/resize.h
    zimg/src/zimg/resize/resize_impl.h
    zimg/src/zimg/resize/x86/resize_impl_avx512_common.h
    zimg/src/zimg/resize/x86/resize_impl_x86.h
    zimg/src/zimg/unresize/bilinear.h
    zimg/src/zimg/unresize/unresize.h
    zimg/src/zimg/unresize/unresize_impl.h
    zimg/src/zimg/unresize/x86/unresize_impl_x86.h
)

target_sources(zimg PRIVATE
    zimg/src/zimg/api/zimg.cpp
    zimg/src/zimg/colorspace/colorspace.cpp
    zimg/src/zimg/colorspace/colorspace_param.cpp
    zimg/src/zimg/colorspace/gamma.cpp
    zimg/src/zimg/colorspace/graph.cpp
    zimg/src/zimg/colorspace/matrix3.cpp
    zimg/src/zimg/colorspace/operation.cpp
    zimg/src/zimg/colorspace/operation_impl.cpp
    zimg/src/zimg/colorspace/x86/operation_impl_sse.cpp
    zimg/src/zimg/colorspace/x86/operation_impl_sse2.cpp
    zimg/src/zimg/colorspace/x86/operation_impl_avx.cpp
    zimg/src/zimg/colorspace/x86/operation_impl_avx2.cpp
    zimg/src/zimg/colorspace/x86/operation_impl_x86.cpp
    zimg/src/zimg/common/cpuinfo.cpp
    zimg/src/zimg/common/libm_wrapper.cpp
    zimg/src/zimg/common/matrix.cpp
    zimg/src/zimg/common/x86/cpuinfo_x86.cpp
    zimg/src/zimg/common/x86/x86util.cpp
    zimg/src/zimg/depth/blue.cpp
    zimg/src/zimg/depth/depth.cpp
    zimg/src/zimg/depth/depth_convert.cpp
    zimg/src/zimg/depth/dither.cpp
    zimg/src/zimg/depth/quantize.cpp
    zimg/src/zimg/depth/x86/depth_convert_sse2.cpp
    zimg/src/zimg/depth/x86/depth_convert_avx2.cpp
    zimg/src/zimg/depth/x86/depth_convert_x86.cpp
    zimg/src/zimg/depth/x86/dither_sse2.cpp
    zimg/src/zimg/depth/x86/dither_avx2.cpp
    zimg/src/zimg/depth/x86/dither_x86.cpp
    zimg/src/zimg/depth/x86/error_diffusion_sse2.cpp
    zimg/src/zimg/depth/x86/error_diffusion_avx2.cpp
    zimg/src/zimg/depth/x86/f16c_sse2.cpp
    zimg/src/zimg/depth/x86/f16c_ivb.cpp
    zimg/src/zimg/graph/basic_filter.cpp
    zimg/src/zimg/graph/filtergraph.cpp
    zimg/src/zimg/graph/graphbuilder.cpp
    zimg/src/zimg/graph/graphnode.cpp
    zimg/src/zimg/resize/filter.cpp
    zimg/src/zimg/resize/resize.cpp
    zimg/src/zimg/resize/resize_impl.cpp
    zimg/src/zimg/resize/x86/resize_impl_sse.cpp
    zimg/src/zimg/resize/x86/resize_impl_sse2.cpp
    zimg/src/zimg/resize/x86/resize_impl_avx.cpp
    zimg/src/zimg/resize/x86/resize_impl_avx2.cpp
    zimg/src/zimg/resize/x86/resize_impl_x86.cpp
    zimg/src/zimg/unresize/bilinear.cpp
    zimg/src/zimg/unresize/unresize.cpp
    zimg/src/zimg/unresize/unresize_impl.cpp
    zimg/src/zimg/unresize/x86/unresize_impl_x86.cpp
    zimg/src/zimg/unresize/x86/unresize_impl_sse.cpp
)

target_compile_definitions(zimg PRIVATE _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS ZIMG_X86)
target_include_directories(zimg PRIVATE zimg/src/zimg)
target_include_directories(zimg INTERFACE zimg/src/zimg/api)
