/*
* Copyright (c) 2015-2017 John Smith & Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/


#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <VapourSynth.h>
#include <VSHelper.h>
#include "cpufeatures.h"
#include "filtershared.h"
#include "filtersharedcpp.h"
#include "kernel/cpulevel.h"
#include "kernel/generic.h"

#ifdef _MSC_VER
#define FORCE_INLINE inline __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }
} // namespace

enum GenericOperations {
    GenericPrewitt,
    GenericSobel,

    GenericMinimum,
    GenericMaximum,

    GenericMedian,

    GenericDeflate,
    GenericInflate,

    GenericConvolution
};

enum ConvolutionTypes {
    ConvolutionSquare,
    ConvolutionHorizontal,
    ConvolutionVertical
};

struct GenericData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    bool process[3];

    const char *filter_name;

    // Prewitt, Sobel
    float scale;

    // Minimum, Maximum, Deflate, Inflate
    uint16_t th;
    float thf;

    // Minimum, Maximum
    uint8_t enable;

    // Convolution
    ConvolutionTypes convolution_type;
    int matrix[25];
    float matrixf[25];
    int matrix_sum;
    int matrix_elements;
    float rdiv;
    float bias;
    bool saturate;

    int cpulevel;
};

template<typename T, typename OP>
static const VSFrameRef *VS_CC singlePixelGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    T *d = reinterpret_cast<T *>(* instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);

        try {
            shared816FFormatCheck(fi);
        } catch (const std::runtime_error &error) {
            vsapi->setFilterError((d->name + ": "_s + error.what()).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = {
            d->process[0] ? nullptr : src,
            d->process[1] ? nullptr : src,
            d->process[2] ? nullptr : src
        };

        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                OP opts(d, fi, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
                const uint8_t * VS_RESTRICT srcp = vsapi->getReadPtr(src, plane);
                int width = vsapi->getFrameWidth(src, plane);
                int height = vsapi->getFrameHeight(src, plane);
                ptrdiff_t stride = vsapi->getStride(src, plane);

                for (int h = 0; h < height; h++) {
                    if (fi->bytesPerSample == 1)
                        OP::template processPlane<uint8_t>(srcp, dstp, width, opts);
                    else if (fi->bytesPerSample == 2)
                        OP::template processPlane<uint16_t>(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), width, opts);
                    else if (fi->bytesPerSample == 4)
                        OP::template processPlaneF<float>(reinterpret_cast<const float *>(srcp), reinterpret_cast<float *>(dstp), width, opts);
                    srcp += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

template<typename T>
static void templateInit(T& d, const char *name, bool allowVariableFormat, const VSMap *in, VSMap *out, const VSAPI *vsapi) {
    *d = {};

    d->name = name;
    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    shared816FFormatCheck(d->vi->format, allowVariableFormat);

    getPlanesArg(in, d->process, vsapi);
}

vs_generic_params make_generic_params(const GenericData *d, const VSFormat *fi, int plane)
{
    vs_generic_params params{};

    params.maxval = ((1 << fi->bitsPerSample) - 1);
    params.scale = d->scale;
    params.threshold = d->th;
    params.thresholdf = d->thf;
    params.stencil = d->enable;

    for (int i = 0; i < d->matrix_elements; ++i) {
        params.matrix[i] = d->matrix[i];
        params.matrixf[i] = d->matrixf[i];
    }
    params.matrixsize = d->matrix_elements;

    params.div = d->rdiv;
    params.bias = d->bias;
    params.saturate = d->saturate;

    return params;
}

#ifdef VS_TARGET_CPU_X86
template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectAVX2(const VSFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_byte_avx2;
        case GenericMinimum: return vs_generic_3x3_min_byte_avx2;
        case GenericMaximum: return vs_generic_3x3_max_byte_avx2;
        case GenericMedian: return vs_generic_3x3_median_byte_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_byte_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_avx2;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_word_avx2;
        case GenericMinimum: return vs_generic_3x3_min_word_avx2;
        case GenericMaximum: return vs_generic_3x3_max_word_avx2;
        case GenericMedian: return vs_generic_3x3_median_word_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_word_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_word_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_avx2;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_float_avx2;
        case GenericMinimum: return vs_generic_3x3_min_float_avx2;
        case GenericMaximum: return vs_generic_3x3_max_float_avx2;
        case GenericMedian: return vs_generic_3x3_median_float_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_float_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_float_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_avx2;
            break;
        }
    }
    return nullptr;
}

template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectSSE2(const VSFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_sse2;
        case GenericSobel: return vs_generic_3x3_sobel_byte_sse2;
        case GenericMinimum: return vs_generic_3x3_min_byte_sse2;
        case GenericMaximum: return vs_generic_3x3_max_byte_sse2;
        case GenericMedian: return vs_generic_3x3_median_byte_sse2;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_sse2;
        case GenericInflate: return vs_generic_3x3_inflate_byte_sse2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_sse2;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_sse2;
        case GenericSobel: return vs_generic_3x3_sobel_word_sse2;
        case GenericMinimum: return vs_generic_3x3_min_word_sse2;
        case GenericMaximum: return vs_generic_3x3_max_word_sse2;
        case GenericMedian: return vs_generic_3x3_median_word_sse2;
        case GenericDeflate: return vs_generic_3x3_deflate_word_sse2;
        case GenericInflate: return vs_generic_3x3_inflate_word_sse2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_sse2;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_sse2;
        case GenericSobel: return vs_generic_3x3_sobel_float_sse2;
        case GenericMinimum: return vs_generic_3x3_min_float_sse2;
        case GenericMaximum: return vs_generic_3x3_max_float_sse2;
        case GenericMedian: return vs_generic_3x3_median_float_sse2;
        case GenericDeflate: return vs_generic_3x3_deflate_float_sse2;
        case GenericInflate: return vs_generic_3x3_inflate_float_sse2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_sse2;
            break;
        }
    }
    return nullptr;
}
#endif

template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectC(const VSFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_c;
        case GenericSobel: return vs_generic_3x3_sobel_byte_c;
        case GenericMinimum: return vs_generic_3x3_min_byte_c;
        case GenericMaximum: return vs_generic_3x3_max_byte_c;
        case GenericMedian: return vs_generic_3x3_median_byte_c;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_c;
        case GenericInflate: return vs_generic_3x3_inflate_byte_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_byte_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_byte_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_byte_c;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_c;
        case GenericSobel: return vs_generic_3x3_sobel_word_c;
        case GenericMinimum: return vs_generic_3x3_min_word_c;
        case GenericMaximum: return vs_generic_3x3_max_word_c;
        case GenericMedian: return vs_generic_3x3_median_word_c;
        case GenericDeflate: return vs_generic_3x3_deflate_word_c;
        case GenericInflate: return vs_generic_3x3_inflate_word_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_word_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_word_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_word_c;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_c;
        case GenericSobel: return vs_generic_3x3_sobel_float_c;
        case GenericMinimum: return vs_generic_3x3_min_float_c;
        case GenericMaximum: return vs_generic_3x3_max_float_c;
        case GenericMedian: return vs_generic_3x3_median_float_c;
        case GenericDeflate: return vs_generic_3x3_deflate_float_c;
        case GenericInflate: return vs_generic_3x3_inflate_float_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_float_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_float_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_float_c;
            break;
        }
    }
    return nullptr;
}

template <GenericOperations op>
static const VSFrameRef *VS_CC genericGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GenericData *d = static_cast<GenericData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);

        try {
            shared816FFormatCheck(fi);
            if (vsapi->getFrameWidth(src, fi->numPlanes - 1) < 4 || vsapi->getFrameHeight(src, fi->numPlanes - 1) < 4)
                throw std::runtime_error("Cannot process frames with subsampled planes smaller than 4x4.");

        } catch (const std::runtime_error &error) {
            vsapi->setFilterError((d->filter_name + ": "_s + error.what()).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return 0;
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = {
            d->process[0] ? nullptr : src,
            d->process[1] ? nullptr : src,
            d->process[2] ? nullptr : src
        };

        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        void (*func)(const void *, ptrdiff_t, void *, ptrdiff_t, const vs_generic_params *, unsigned, unsigned) = nullptr;

#ifdef VS_TARGET_CPU_X86
        if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2)
            func = genericSelectAVX2<op>(fi, d);
        if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2)
            func = genericSelectSSE2<op>(fi, d);
#endif
        if (!func)
            func = genericSelectC<op>(fi, d);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (func && d->process[plane]) {
                uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                int width = vsapi->getFrameWidth(src, plane);
                int height = vsapi->getFrameHeight(src, plane);
                int src_stride = vsapi->getStride(src, plane);
                int dst_stride = vsapi->getStride(dst, plane);

                vs_generic_params params = make_generic_params(d, fi, plane);
                func(srcp, src_stride, dstp, dst_stride, &params, width, height);
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

template <GenericOperations op>
static void VS_CC genericCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<GenericData> d(new GenericData{});

    d->filter_name = static_cast<const char *>(userData);

    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);

    try {
        shared816FFormatCheck(d->vi->format);

        if (d->vi->height && d->vi->width)
            if (planeWidth(d->vi, d->vi->format->numPlanes - 1) < 4 || planeHeight(d->vi, d->vi->format->numPlanes - 1) < 4)
                throw std::runtime_error("Cannot process frames with subsampled planes smaller than 4x4.");

        getPlanesArg(in, d->process, vsapi);

        int err;

        if (op == GenericMinimum || op == GenericMaximum || op == GenericDeflate || op == GenericInflate) {
            d->thf = static_cast<float>(vsapi->propGetFloat(in, "threshold", 0, &err));
            if (err) {
                d->th = ((1 << d->vi->format->bitsPerSample) - 1);
                d->thf = std::numeric_limits<float>::max();
            } else {
                if (d->vi->format->sampleType == stInteger) {
                    int64_t ith = floatToInt64S(d->thf);
                    if (ith < 0 || ith > ((1 << d->vi->format->bitsPerSample) - 1))
                        throw std::runtime_error("threshold bigger than sample value.");
                    d->th = static_cast<uint16_t>(ith);
                } else {
                    if (d->thf < 0)
                        throw std::runtime_error("threshold must be a positive value.");
                }
            }
        }

        if (op == GenericMinimum || op == GenericMaximum) {
            int enable_elements = vsapi->propNumElements(in, "coordinates");
            if (enable_elements == -1) {
                d->enable = 0xFF;
            } else if (enable_elements == 8) {
                const int64_t *enable = vsapi->propGetIntArray(in, "coordinates", &err);
                for (int i = 0; i < 8; i++) {
                    d->enable |= enable[i] ? (1U << i) : 0U;
                }
            } else {
                throw std::runtime_error("coordinates must contain exactly 8 numbers.");
            }
        }


        if (op == GenericPrewitt || op == GenericSobel) {
            d->scale = vsapi->propGetFloat(in, "scale", 0, &err);
            if (err)
                d->scale = 1.0f;

            if (d->scale < 0)
                throw std::runtime_error("scale must not be negative.");
        }

        if (op == GenericConvolution) {
            d->bias = static_cast<float>(vsapi->propGetFloat(in, "bias", 0, &err));

            d->saturate = !!vsapi->propGetInt(in, "saturate", 0, &err);
            if (err)
                d->saturate = true;

            d->matrix_elements = vsapi->propNumElements(in, "matrix");

            const char *mode = vsapi->propGetData(in, "mode", 0, &err);
            if (err || mode[0] == 's') {
                d->convolution_type = ConvolutionSquare;

                if (d->matrix_elements != 9 && d->matrix_elements != 25)
                    throw std::runtime_error("When mode starts with 's', matrix must contain exactly 9 or exactly 25 numbers.");
            } else if (mode[0] == 'h' || mode[0] == 'v') {
                if (mode[0] == 'h')
                    d->convolution_type = ConvolutionHorizontal;
                else
                    d->convolution_type = ConvolutionVertical;

                if (d->matrix_elements < 3 || d->matrix_elements > 25)
                    throw std::runtime_error("When mode starts with 'h' or 'v', matrix must contain between 3 and 25 numbers.");

                if (d->matrix_elements % 2 == 0)
                    throw std::runtime_error("matrix must contain an odd number of numbers.");
            } else {
                throw std::runtime_error("mode must start with 's', 'h', or 'v'.");
            }

            float matrix_sumf = 0;
            d->matrix_sum = 0;
            const double *matrix = vsapi->propGetFloatArray(in, "matrix", nullptr);
            for (int i = 0; i < d->matrix_elements; i++) {
                if (d->vi->format->sampleType == stInteger) {
                    d->matrix[i] = lround(matrix[i]);
                    d->matrixf[i] = d->matrix[i];
                    if (d->vi->format->sampleType == stInteger && std::abs(d->matrix[i]) > 1023)
                        throw std::runtime_error("coefficients may only be between -1023 and 1023");
                } else {
                    d->matrix[i] = lround(matrix[i]);
                    d->matrixf[i] = static_cast<float>(matrix[i]);
                }

                matrix_sumf += d->matrixf[i];
                d->matrix_sum += d->matrix[i];
            }

            if (std::abs(matrix_sumf) < std::numeric_limits<float>::epsilon())
                matrix_sumf = 1.0;

            d->rdiv = static_cast<float>(vsapi->propGetFloat(in, "divisor", 0, &err));
            if (d->rdiv == 0.0f)
                d->rdiv = static_cast<float>(matrix_sumf);

            d->rdiv = 1.0f / d->rdiv;

            if (op == GenericConvolution && d->convolution_type == ConvolutionHorizontal && d->matrix_elements == 3) {
                //rewrite to 3x3 matrix here
                d->convolution_type = ConvolutionSquare;
                d->matrix_elements = 9;
                d->matrix[3] = d->matrix[0];
                d->matrix[4] = d->matrix[1];
                d->matrix[5] = d->matrix[2];
                d->matrix[0] = 0;
                d->matrix[1] = 0;
                d->matrix[2] = 0;
                d->matrix[6] = 0;
                d->matrix[7] = 0;
                d->matrix[8] = 0;
                d->matrixf[3] = d->matrixf[0];
                d->matrixf[4] = d->matrixf[1];
                d->matrixf[5] = d->matrixf[2];
                d->matrixf[0] = 0.f;
                d->matrixf[1] = 0.f;
                d->matrixf[2] = 0.f;
                d->matrixf[6] = 0.f;
                d->matrixf[7] = 0.f;
                d->matrixf[8] = 0.f;
            } else if (op == GenericConvolution && d->convolution_type == ConvolutionVertical && d->matrix_elements == 3) {
                //rewrite to 3x3 matrix here
                d->convolution_type = ConvolutionSquare;
                d->matrix_elements = 9;
                d->matrix[7] = d->matrix[2];
                d->matrix[4] = d->matrix[1];
                d->matrix[1] = d->matrix[0];
                d->matrix[0] = 0;
                d->matrix[2] = 0;
                d->matrix[3] = 0;
                d->matrix[5] = 0;
                d->matrix[6] = 0;
                d->matrix[8] = 0;
                d->matrixf[7] = d->matrixf[2];
                d->matrixf[4] = d->matrixf[1];
                d->matrixf[1] = d->matrixf[0];
                d->matrixf[0] = 0.f;
                d->matrixf[2] = 0.f;
                d->matrixf[3] = 0.f;
                d->matrixf[5] = 0.f;
                d->matrixf[6] = 0.f;
                d->matrixf[8] = 0.f;
            }
        }

        if (op == GenericConvolution && d->convolution_type == ConvolutionHorizontal && d->matrix_elements / 2 >= planeWidth(d->vi, d->vi->format->numPlanes - 1))
            throw std::runtime_error("Width must be bigger than convolution radius.");
        if (op == GenericConvolution && d->convolution_type == ConvolutionVertical && d->matrix_elements / 2 >= planeHeight(d->vi, d->vi->format->numPlanes - 1))
            throw std::runtime_error("Height must be bigger than convolution radius.");

        d->cpulevel = vs_get_cpulevel(core);
    } catch (const std::runtime_error &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, (d->filter_name + ": "_s + error.what()).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->filter_name, templateNodeInit<GenericData>, genericGetframe<op>, templateNodeFree<GenericData>, fmParallel, 0, d.get(), core);
    d.release();
}

///////////////////////////////


struct InvertData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    bool mask;
};

struct InvertOp {
    uint16_t max;
    bool uv;

    InvertOp(InvertData *d, const VSFormat *fi, int plane) {
        uv = (!d->mask) && ((fi->colorFamily == cmYUV) || (fi->colorFamily == cmYCoCg)) && (plane > 0);
        max = (1LL << fi->bitsPerSample) - 1;
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const InvertOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = static_cast<T>(opts.max) - std::min(src[w], static_cast<T>(opts.max));
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const InvertOp &opts) {
        if (opts.uv) {
            for (unsigned w = 0; w < width; w++)
                dst[w] = -src[w];
        } else {
            for (unsigned w = 0; w < width; w++)
                dst[w] = 1 - src[w];
        }
    }
};

static void VS_CC invertCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<InvertData> d(new InvertData);

    try {
        templateInit(d, userData ? "InvertMask" : "Invert", true, in, out, vsapi);
        d->mask = !!userData;
    } catch (const std::runtime_error &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, (d->name + ": "_s + error.what()).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateNodeInit<InvertData>, singlePixelGetFrame<InvertData, InvertOp>, templateNodeFree<InvertData>, fmParallel, 0, d.get(), core);
    d.release();
}

/////////////////

struct LimitData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    uint16_t max[3], min[3];
    float maxf[3], minf[3];
};

struct LimitOp {
    uint16_t max, min;
    float maxf, minf;

    LimitOp(LimitData *d, const VSFormat *fi, int plane) {
        max = d->max[plane];
        min = d->min[plane];
        maxf = d->maxf[plane];
        minf = d->minf[plane];
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const LimitOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = std::min(static_cast<T>(opts.max), std::max(static_cast<T>(opts.min), src[w]));
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const LimitOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = std::min(opts.maxf, std::max(opts.minf, src[w]));
    }
};

static void VS_CC limitCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LimitData> d(new LimitData);

    try {
        templateInit(d, "Limiter", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "min", d->min, d->minf, RangeLower, false, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "max", d->max, d->maxf, RangeUpper, false, vsapi);
        for (int i = 0; i < 3; i++)
            if (((d->vi->format->sampleType == stInteger) && (d->min[i] > d->max[i])) || ((d->vi->format->sampleType == stFloat) && (d->minf[i] > d->maxf[i])))
                throw std::runtime_error("min bigger than max");
    } catch (const std::runtime_error &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, (d->name + ": "_s + error.what()).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateNodeInit<LimitData>, singlePixelGetFrame<LimitData, LimitOp>, templateNodeFree<LimitData>, fmParallel, 0, d.get(), core);
    d.release();
}

/////////////////

struct BinarizeData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    uint16_t v0[3], v1[3], thr[3];
    float v0f[3], v1f[3], thrf[3];
};

struct BinarizeOp {
    uint16_t v0, v1, thr;
    float v0f, v1f, thrf;

    BinarizeOp(BinarizeData *d, const VSFormat *fi, int plane) {
        v0 = d->v0[plane];
        v1 = d->v1[plane];
        v0f = d->v0f[plane];
        v1f = d->v1f[plane];
        thr = d->thr[plane];
        thrf = d->thrf[plane];
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const BinarizeOp &opts) {
        for (unsigned w = 0; w < width; w++)
            if (src[w] < opts.thr)
                dst[w] = static_cast<T>(opts.v0);
            else
                dst[w] = static_cast<T>(opts.v1);
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const BinarizeOp &opts) {
        for (unsigned w = 0; w < width; w++)
            if (src[w] < opts.thrf)
                dst[w] = opts.v0f;
            else
                dst[w] = opts.v1f;
    }
};

static void VS_CC binarizeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BinarizeData> d(new BinarizeData);

    try {
        templateInit(d, userData ? "BinarizeMask" : "Binarize", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "v0", d->v0, d->v0f, RangeLower, !!userData, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "v1", d->v1, d->v1f, RangeUpper, !!userData, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "threshold", d->thr, d->thrf, RangeMiddle, !!userData, vsapi);
    } catch (const std::runtime_error &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, (d->name + ": "_s + error.what()).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateNodeInit<BinarizeData>, singlePixelGetFrame<BinarizeData, BinarizeOp>, templateNodeFree<BinarizeData>, fmParallel, 0, d.get(), core);
    d.release();
}

/////////////////

struct LevelsData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    float gamma;
    float max_in, max_out, min_in, min_out;
    std::vector<uint8_t> lut;
};

template<typename T>
static const VSFrameRef *VS_CC levelsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = reinterpret_cast<LevelsData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = { d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src };
        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                const T * VS_RESTRICT srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
                int src_stride = vsapi->getStride(src, plane);
                T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));
                int dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                T maxval = static_cast<T>((static_cast<int64_t>(1) << fi->bitsPerSample) - 1);
                const T * VS_RESTRICT lut = reinterpret_cast<const T *>(d->lut.data());

                for (int hl = 0; hl < h; hl++) {
                    for (int x = 0; x < w; x++)
                        dstp[x] = lut[std::min(srcp[x], maxval)];

                    dstp += dst_stride / sizeof(T);
                    srcp += src_stride / sizeof(T);
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

template<typename T>
static const VSFrameRef *VS_CC levelsGetframeF(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = reinterpret_cast<LevelsData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = { d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src };
        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                const T * VS_RESTRICT srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
                int src_stride = vsapi->getStride(src, plane);
                T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));
                int dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                T gamma = d->gamma;
                T range_in = 1.f / (d->max_in - d->min_in);
                T range_out = d->max_out - d->min_out;
                T min_in = d->min_in;
                T min_out = d->min_out;
                T max_in = d->max_in;

                if (std::abs(d->gamma - static_cast<T>(1.0)) < std::numeric_limits<T>::epsilon()) {
                    T range_scale = range_out / (d->max_in - d->min_in);
                    for (int hl = 0; hl < h; hl++) {
                        for (int x = 0; x < w; x++)
                            dstp[x] = (std::max(std::min(srcp[x], max_in) - min_in, 0.f)) * range_scale + min_out;

                        dstp += dst_stride / sizeof(T);
                        srcp += src_stride / sizeof(T);
                    }
                } else {
                    for (int hl = 0; hl < h; hl++) {
                        for (int x = 0; x < w; x++)
                            dstp[x] = std::pow((std::max(std::min(srcp[x], max_in) - min_in, 0.f)) * range_in, gamma) * range_out + min_out;

                        dstp += dst_stride / sizeof(T);
                        srcp += src_stride / sizeof(T);
                    }
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC levelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LevelsData> d(new LevelsData);

    try {
        templateInit(d, "Levels", false, in, out, vsapi);
    } catch (const std::runtime_error &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, (d->name + ": "_s + error.what()).c_str());
        return;
    }

    int err;
    float maxvalf = 1.0f;
    if (d->vi->format->sampleType == stInteger)
        maxvalf = (1 << d->vi->format->bitsPerSample) - 1;

    d->min_in = static_cast<float>(vsapi->propGetFloat(in, "min_in", 0, &err));
    d->min_out = static_cast<float>(vsapi->propGetFloat(in, "min_out", 0, &err));
    d->max_in = static_cast<float>(vsapi->propGetFloat(in, "max_in", 0, &err));
    if (err)
        d->max_in = maxvalf;
    d->max_out = static_cast<float>(vsapi->propGetFloat(in, "max_out", 0, &err));
    if (err)
        d->max_out = maxvalf;
    d->gamma = static_cast<float>(vsapi->propGetFloat(in, "gamma", 0, &err));
    if (err)
        d->gamma = 1.f;
    else
        d->gamma = 1.f / d->gamma;

    // Implement with simple lut for integer
    if (d->vi->format->sampleType == stInteger) {
        int maxval = (1 << d->vi->format->bitsPerSample) - 1;
        d->lut.resize(d->vi->format->bytesPerSample * (1 << d->vi->format->bitsPerSample));

        d->min_in = std::round(d->min_in);
        d->min_out = std::round(d->min_out);
        d->max_in = std::round(d->max_in);
        d->max_out = std::round(d->max_out);

        if (d->vi->format->bytesPerSample == 1) {
            for (int v = 0; v <= 255; v++)
                d->lut[v] = static_cast<uint8_t>(std::max(std::min(std::pow(std::max(std::min<float>(v, d->max_in) - d->min_in, 0.f) / (d->max_in - d->min_in), d->gamma) * (d->max_out - d->min_out) + d->min_out, 255.f), 0.f) + 0.5f);
        } else {
            uint16_t *lptr = reinterpret_cast<uint16_t *>(d->lut.data());
            for (int v = 0; v <= maxval; v++)
                lptr[v] = static_cast<uint16_t>(std::max(std::min(std::pow(std::max(std::min<float>(v, d->max_in) - d->min_in, 0.f) / (d->max_in - d->min_in), d->gamma) * (d->max_out - d->min_out) + d->min_out, maxvalf), 0.f) + 0.5f);
        }
    }

    if (d->vi->format->bytesPerSample == 1)
        vsapi->createFilter(in, out, d->name, templateNodeInit<LevelsData>, levelsGetframe<uint8_t>, templateNodeFree<LevelsData>, fmParallel, 0, d.get(), core);
    else if (d->vi->format->bytesPerSample == 2)
        vsapi->createFilter(in, out, d->name, templateNodeInit<LevelsData>, levelsGetframe<uint16_t>, templateNodeFree<LevelsData>, fmParallel, 0, d.get(), core);
    else
        vsapi->createFilter(in, out, d->name, templateNodeInit<LevelsData>, levelsGetframeF<float>, templateNodeFree<LevelsData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////

VS_API(void) genericInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Minimum",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            "coordinates:int[]:opt;"
            , genericCreate<GenericMinimum>, const_cast<char *>("Minimum"), plugin);

    registerFunc("Maximum",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            "coordinates:int[]:opt;"
            , genericCreate<GenericMaximum>, const_cast<char *>("Maximum"), plugin);

    registerFunc("Median",
            "clip:clip;"
            "planes:int[]:opt;"
            , genericCreate<GenericMedian>, const_cast<char *>("Median"), plugin);

    registerFunc("Deflate",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            , genericCreate<GenericDeflate>, const_cast<char *>("Deflate"), plugin);

    registerFunc("Inflate",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            , genericCreate<GenericInflate>, const_cast<char *>("Inflate"), plugin);

    registerFunc("Convolution",
            "clip:clip;"
            "matrix:float[];"
            "bias:float:opt;"
            "divisor:float:opt;"
            "planes:int[]:opt;"
            "saturate:int:opt;"
            "mode:data:opt;"
            , genericCreate<GenericConvolution>, const_cast<char *>("Convolution"), plugin);

    registerFunc("Prewitt",
            "clip:clip;"
            "planes:int[]:opt;"
            "scale:float:opt;"
            , genericCreate<GenericPrewitt>, const_cast<char *>("Prewitt"), plugin);

    registerFunc("Sobel",
            "clip:clip;"
            "planes:int[]:opt;"
            "scale:float:opt;"
            , genericCreate<GenericSobel>, const_cast<char *>("Sobel"), plugin);

    registerFunc("Invert",
        "clip:clip;"
        "planes:int[]:opt;"
        , invertCreate, nullptr, plugin);

    registerFunc("InvertMask",
        "clip:clip;"
        "planes:int[]:opt;"
        , invertCreate, (void *)1, plugin);

    registerFunc("Limiter",
        "clip:clip;"
        "min:float[]:opt;"
        "max:float[]:opt;"
        "planes:int[]:opt;"
        , limitCreate, nullptr, plugin);

    registerFunc("Binarize",
        "clip:clip;"
        "threshold:float[]:opt;"
        "v0:float[]:opt;"
        "v1:float[]:opt;"
        "planes:int[]:opt;"
        , binarizeCreate, nullptr, plugin);

    registerFunc("BinarizeMask",
        "clip:clip;"
        "threshold:float[]:opt;"
        "v0:float[]:opt;"
        "v1:float[]:opt;"
        "planes:int[]:opt;"
        , binarizeCreate, (void *)1, plugin);

    registerFunc("Levels",
        "clip:clip;"
        "min_in:float[]:opt;"
        "max_in:float[]:opt;"
        "gamma:float[]:opt;"
        "min_out:float[]:opt;"
        "max_out:float[]:opt;"
        "planes:int[]:opt;"
        , levelsCreate, nullptr, plugin);
}
