#pragma once

#if defined(__clang__) || defined(__GNUC__)

#define LW_VECTOR128 __attribute__((__vector_size__(16)))
#define LW_INTRIN __attribute__((__always_inline__, __nodebug__))
#define LW_MAY_ALIAS __attribute__((__may_alias__))
#define LW_PACKED __attribute__((__packed__))

typedef float m128 LW_VECTOR128;

static LW_INTRIN m128 mm_load64(const void* p) {
    struct h { double v; } LW_MAY_ALIAS LW_PACKED;
    return reinterpret_cast<m128>((double LW_VECTOR128) {static_cast<const h*>(p)->v, 0});
}

static LW_INTRIN void mm_store64(void* p, m128 a) {
    struct h { double v; } LW_MAY_ALIAS LW_PACKED;
    static_cast<h*>(p)->v = reinterpret_cast<double LW_VECTOR128>(a)[0];
}

static LW_INTRIN m128 mm_load128(const void* p) {
    struct h { m128 v; } LW_MAY_ALIAS LW_PACKED;
    return static_cast<const h*>(p)->v;
}

static LW_INTRIN void mm_store128(void* p, m128 a) {
    struct h { m128 v; } LW_MAY_ALIAS LW_PACKED;
    static_cast<h*>(p)->v = a;
}

#define mm_shuffle(a, b, x, y, z, w) __builtin_shufflevector(a, b, x, y, z, w)

static LW_INTRIN m128 mm_mul(m128 a, m128 b) {
    return a * b;
}

static LW_INTRIN m128 mm_fmadd(m128 a, m128 b, m128 c) {
    // XXX: gcc cannot optimize this to fma sometimes
    return a * b + c;
}

#else

#include <emmintrin.h>
#include <immintrin.h>
#include <xmmintrin.h>

#define LW_INTRIN __forceinline
#define LW_MAY_ALIAS /* XXX: not implemented in MSVC */

#define m128 __m128

#define mm_load64(p) _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const double*>(p)))
#define mm_store64(p, a) _mm_store_sd(reinterpret_cast<double*>(p), _mm_castps_pd(a))
#define mm_load128(p) _mm_loadu_ps(reinterpret_cast<const float*>(p))
#define mm_store128(p, a) _mm_storeu_ps(reinterpret_cast<float*>(p), a)

#define _lw_neg_to_zero(v) ((v) < 0 ? 0 : (v))
#define mm_shuffle(a, b, x, y, z, w) _mm_shuffle_ps(a, b, _lw_neg_to_zero(x) | (_lw_neg_to_zero(y) << 2) | (_lw_neg_to_zero(z) << 4) | (_lw_neg_to_zero(w) << 6))

#define mm_mul _mm_mul_ps
#define mm_fmadd _mm_fmadd_ps

#endif
