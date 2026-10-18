/* Packed single and double floating point: arithmetic, bitwise, conversions, compares,
   movmsk, and the SSE3 addsub and horizontal forms. */
#include "corpus.h"
#include <emmintrin.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

#define SSE3 __attribute__((target("sse3")))

/* Read-only operands, so the compiler can use them as memory operands. */
static const float K_PS[4] __attribute__((aligned(16))) = {1.5f, -2.25f, 3.0f, 0.375f};
static const float K_PS2[4] __attribute__((aligned(16))) = {-0.625f, 7.0f, 0.1f, -12.5f};
static const double K_PD[2] __attribute__((aligned(16))) = {1.5, -2.25};
static const double K_PD2[2] __attribute__((aligned(16))) = {0.1, -12.5};
static const i32 K_I32[4] __attribute__((aligned(16))) = {7, -3, 1000, -1};

/* ---- packed single: arithmetic ---- */

EXPORT void fp_addps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_add_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_addps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_add_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
EXPORT void fp_addps_chain(float *o, const float *a, const float *b) {
    __m128 x = _mm_add_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2));
    _mm_storeu_ps(o, _mm_add_ps(x, _mm_mul_ps(x, _mm_loadu_ps(b))));
}
EXPORT void fp_subps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_sub_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_subps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_sub_ps(_mm_load_ps(K_PS), _mm_loadu_ps(a)));
}
EXPORT void fp_subps_chain(float *o, const float *a, const float *b) {
    __m128 x = _mm_sub_ps(_mm_loadu_ps(a), _mm_loadu_ps(b));
    _mm_storeu_ps(o, _mm_sub_ps(_mm_mul_ps(x, x), _mm_load_ps(K_PS2)));
}
EXPORT void fp_mulps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_mul_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_mulps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_mul_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2)));
}
EXPORT void fp_mulps_chain(float *o, const float *a, const float *b) {
    __m128 x = _mm_mul_ps(_mm_loadu_ps(a), _mm_loadu_ps(b));
    _mm_storeu_ps(o, _mm_mul_ps(_mm_add_ps(x, x), _mm_loadu_ps(a)));
}
EXPORT void fp_divps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_div_ps(_mm_loadu_ps(a), _mm_or_ps(_mm_loadu_ps(b), _mm_set1_ps(1.0f))));
}
EXPORT void fp_divps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_div_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
EXPORT void fp_sqrtps(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_sqrt_ps(_mm_loadu_ps(a)));
}
EXPORT void fp_sqrtps_abs(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_sqrt_ps(_mm_and_ps(_mm_loadu_ps(a), _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)))));
}
EXPORT void fp_sqrtps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_sqrt_ps(_mm_add_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS))));
}
EXPORT void fp_minps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_min_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_minps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_min_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2)));
}
EXPORT void fp_maxps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_max_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_maxps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_max_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2)));
}
EXPORT void fp_rcpps(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_rcp_ps(_mm_loadu_ps(a)));
}
EXPORT void fp_rcpps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_rcp_ps(_mm_add_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS))));
}
EXPORT void fp_rsqrtps(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_rsqrt_ps(_mm_loadu_ps(a)));
}
EXPORT void fp_rsqrtps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_rsqrt_ps(_mm_add_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2))));
}
EXPORT void fp_dot4_ps(float *o, const float *a, const float *b) {
    __m128 p = _mm_mul_ps(_mm_loadu_ps(a), _mm_loadu_ps(b));
    p = _mm_add_ps(p, _mm_shuffle_ps(p, p, _MM_SHUFFLE(1, 0, 3, 2)));
    p = _mm_add_ps(p, _mm_shuffle_ps(p, p, _MM_SHUFFLE(2, 3, 0, 1)));
    _mm_store_ss(o, p);
}

/* ---- packed double: arithmetic ---- */

EXPORT void fp_addpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_add_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_addpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_add_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD)));
}
EXPORT void fp_addpd_chain(double *o, const double *a, const double *b) {
    __m128d x = _mm_add_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2));
    _mm_storeu_pd(o, _mm_add_pd(x, _mm_mul_pd(x, _mm_loadu_pd(b))));
}
EXPORT void fp_subpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_sub_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_subpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_sub_pd(_mm_load_pd(K_PD), _mm_loadu_pd(a)));
}
EXPORT void fp_mulpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_mul_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_mulpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2)));
}
EXPORT void fp_mulpd_chain(double *o, const double *a, const double *b) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_sub_pd(_mm_mul_pd(x, x), _mm_loadu_pd(a)));
}
EXPORT void fp_divpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_div_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_divpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_div_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD)));
}
EXPORT void fp_sqrtpd(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_sqrt_pd(_mm_loadu_pd(a)));
}
EXPORT void fp_sqrtpd_abs(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_sqrt_pd(_mm_and_pd(_mm_loadu_pd(a), _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffffLL)))));
}
EXPORT void fp_sqrtpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_sqrt_pd(_mm_add_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD))));
}
EXPORT void fp_minpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_min_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_minpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_min_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2)));
}
EXPORT void fp_maxpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_max_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_maxpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_max_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2)));
}

/* ---- bitwise, single and double ---- */

EXPORT void fp_andps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_and_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_andps_abs(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_and_ps(_mm_loadu_ps(a), _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff))));
}
EXPORT void fp_andps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_and_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
EXPORT void fp_andnps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_andnot_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_andnps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_andnot_ps(_mm_load_ps(K_PS2), _mm_loadu_ps(a)));
}
EXPORT void fp_orps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_or_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_orps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_or_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
EXPORT void fp_xorps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_xor_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_xorps_neg(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_xor_ps(_mm_loadu_ps(a), _mm_set1_ps(-0.0f)));
}
EXPORT void fp_xorps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_xor_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2)));
}

EXPORT void fp_andpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_and_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_andpd_abs(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_and_pd(_mm_loadu_pd(a), _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffffLL))));
}
EXPORT void fp_andpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_and_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD)));
}
EXPORT void fp_andnpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_andnot_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_andnpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_andnot_pd(_mm_load_pd(K_PD2), _mm_loadu_pd(a)));
}
EXPORT void fp_orpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_or_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_orpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_or_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD)));
}
EXPORT void fp_xorpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_xor_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_xorpd_neg(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_xor_pd(_mm_loadu_pd(a), _mm_set1_pd(-0.0)));
}
EXPORT void fp_xorpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_xor_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2)));
}

/* Bitwise ops between double arithmetic: the compiler picks the pd forms (andpd, orpd, ...)
   when the neighbours are pd, and the ps forms when they are not. */
EXPORT void fp_andpd_mix(double *o, const double *a, const double *b) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_add_pd(_mm_and_pd(x, _mm_loadu_pd(b)), _mm_loadu_pd(a)));
}
EXPORT void fp_andpd_mix_k(double *o, const double *a) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2));
    _mm_storeu_pd(o, _mm_mul_pd(_mm_and_pd(x, _mm_load_pd(K_PD)), x));
}
EXPORT void fp_andnpd_mix(double *o, const double *a, const double *b) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_add_pd(_mm_andnot_pd(x, _mm_loadu_pd(b)), _mm_loadu_pd(a)));
}
EXPORT void fp_andnpd_mix_k(double *o, const double *a) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2));
    _mm_storeu_pd(o, _mm_add_pd(_mm_andnot_pd(_mm_load_pd(K_PD), x), x));
}
EXPORT void fp_andnpd_mix2(double *o, const double *a, const double *b) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_mul_pd(_mm_andnot_pd(_mm_loadu_pd(a), x), _mm_loadu_pd(b)));
}
EXPORT void fp_andnpd_sub(double *o, const double *a, const double *b) {
    __m128d x = _mm_sub_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_sub_pd(_mm_andnot_pd(x, _mm_loadu_pd(a)), x));
}
EXPORT void fp_orpd_mix(double *o, const double *a, const double *b) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_add_pd(_mm_or_pd(x, _mm_loadu_pd(b)), _mm_loadu_pd(a)));
}
EXPORT void fp_orpd_mix_k(double *o, const double *a) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2));
    _mm_storeu_pd(o, _mm_mul_pd(_mm_or_pd(x, _mm_load_pd(K_PD)), _mm_load_pd(K_PD)));
}
EXPORT void fp_xorpd_mix(double *o, const double *a, const double *b) {
    __m128d x = _mm_add_pd(_mm_loadu_pd(a), _mm_loadu_pd(b));
    _mm_storeu_pd(o, _mm_mul_pd(_mm_xor_pd(x, _mm_loadu_pd(b)), _mm_loadu_pd(a)));
}
EXPORT void fp_xorpd_mix_k(double *o, const double *a) {
    __m128d x = _mm_add_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD));
    _mm_storeu_pd(o, _mm_sub_pd(_mm_xor_pd(x, _mm_load_pd(K_PD2)), _mm_loadu_pd(a)));
}
EXPORT void fp_xorpd_negmix(double *o, const double *a) {
    __m128d x = _mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD));
    _mm_storeu_pd(o, _mm_add_pd(_mm_xor_pd(x, _mm_set1_pd(-0.0)), _mm_loadu_pd(a)));
}

/* ---- conversions ---- */

EXPORT void fp_cvtdq2ps(float *o, const i32 *a) {
    _mm_storeu_ps(o, _mm_cvtepi32_ps(_mm_loadu_si128((const __m128i *)a)));
}
EXPORT void fp_cvtdq2ps_k(float *o, const i32 *a) {
    _mm_storeu_ps(o, _mm_cvtepi32_ps(_mm_add_epi32(_mm_loadu_si128((const __m128i *)a), _mm_load_si128((const __m128i *)K_I32))));
}
EXPORT void fp_cvtps2dq(i32 *o, const float *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvtps_epi32(_mm_loadu_ps(a)));
}
EXPORT void fp_cvtps2dq_k(i32 *o, const float *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvtps_epi32(_mm_mul_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS))));
}
EXPORT void fp_cvttps2dq(i32 *o, const float *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvttps_epi32(_mm_loadu_ps(a)));
}
EXPORT void fp_cvttps2dq_k(i32 *o, const float *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvttps_epi32(_mm_mul_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2))));
}
EXPORT void fp_cvtdq2pd(double *o, const i32 *a) {
    _mm_storeu_pd(o, _mm_cvtepi32_pd(_mm_loadu_si128((const __m128i *)a)));
}
EXPORT void fp_cvtdq2pd_k(double *o, const i32 *a) {
    _mm_storeu_pd(o, _mm_cvtepi32_pd(_mm_add_epi32(_mm_loadu_si128((const __m128i *)a), _mm_load_si128((const __m128i *)K_I32))));
}
EXPORT void fp_cvtpd2dq(i32 *o, const double *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvtpd_epi32(_mm_loadu_pd(a)));
}
EXPORT void fp_cvtpd2dq_k(i32 *o, const double *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvtpd_epi32(_mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD))));
}
EXPORT void fp_cvttpd2dq(i32 *o, const double *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvttpd_epi32(_mm_loadu_pd(a)));
}
EXPORT void fp_cvttpd2dq_k(i32 *o, const double *a) {
    _mm_storeu_si128((__m128i *)o, _mm_cvttpd_epi32(_mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2))));
}
EXPORT void fp_cvtps2pd(double *o, const float *a) {
    _mm_storeu_pd(o, _mm_cvtps_pd(_mm_loadu_ps(a)));
}
EXPORT void fp_cvtps2pd_k(double *o, const float *a) {
    _mm_storeu_pd(o, _mm_cvtps_pd(_mm_add_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2))));
}
EXPORT void fp_cvtpd2ps(float *o, const double *a) {
    _mm_storeu_ps(o, _mm_cvtpd_ps(_mm_loadu_pd(a)));
}
EXPORT void fp_cvtpd2ps_k(float *o, const double *a) {
    _mm_storeu_ps(o, _mm_cvtpd_ps(_mm_mul_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2))));
}
EXPORT void fp_cvt_roundtrip(float *o, const float *a) {
    __m128d d = _mm_mul_pd(_mm_cvtps_pd(_mm_loadu_ps(a)), _mm_load_pd(K_PD));
    _mm_storeu_ps(o, _mm_cvtpd_ps(d));
}

/* ---- sign-bit masks ---- */

EXPORT i32 fp_movmskps(const float *a) {
    return _mm_movemask_ps(_mm_loadu_ps(a));
}
EXPORT i32 fp_movmskps_cmp(const float *a, const float *b) {
    return _mm_movemask_ps(_mm_cmplt_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT i32 fp_movmskpd(const double *a) {
    return _mm_movemask_pd(_mm_loadu_pd(a));
}
EXPORT i32 fp_movmskpd_cmp(const double *a, const double *b) {
    return _mm_movemask_pd(_mm_cmplt_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}

/* ---- SSE3 ---- */

SSE3 EXPORT void fp_addsubps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_addsub_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
SSE3 EXPORT void fp_addsubps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_addsub_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
SSE3 EXPORT void fp_haddps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_hadd_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
SSE3 EXPORT void fp_haddps_self(float *o, const float *a) {
    __m128 x = _mm_loadu_ps(a);
    _mm_storeu_ps(o, _mm_hadd_ps(x, x));
}
SSE3 EXPORT void fp_haddps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_hadd_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2)));
}
SSE3 EXPORT void fp_hadd_dot(float *o, const float *a, const float *b) {
    __m128 p = _mm_mul_ps(_mm_loadu_ps(a), _mm_loadu_ps(b));
    p = _mm_hadd_ps(p, p);
    _mm_storeu_ps(o, _mm_hadd_ps(p, p));
}

/* ---- compares: the mask result is stored, so every predicate's value is observable ---- */

EXPORT void fp_cmpeqps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmpeq_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpltps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmplt_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpleps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmple_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpunordps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmpunord_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpneqps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmpneq_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpnltps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmpnlt_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpnleps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmpnle_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpordps(float *o, const float *a, const float *b) {
    _mm_storeu_ps(o, _mm_cmpord_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
}
EXPORT void fp_cmpeqpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmpeq_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmpltpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmplt_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmplepd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmple_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmpunordpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmpunord_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmpneqpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmpneq_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmpnltpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmpnlt_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmpnlepd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmpnle_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}
EXPORT void fp_cmpordpd(double *o, const double *a, const double *b) {
    _mm_storeu_pd(o, _mm_cmpord_pd(_mm_loadu_pd(a), _mm_loadu_pd(b)));
}

/* Predicate chosen at run time: one branch per predicate, each its own compare. */
EXPORT void fp_cmpps_sel(float *o, const float *a, const float *b, u32 sel) {
    __m128 x = _mm_loadu_ps(a), y = _mm_loadu_ps(b), r;
    switch (sel & 7) {
    case 0: r = _mm_cmpeq_ps(x, y); break;
    case 1: r = _mm_cmplt_ps(x, y); break;
    case 2: r = _mm_cmple_ps(x, y); break;
    case 3: r = _mm_cmpunord_ps(x, y); break;
    case 4: r = _mm_cmpneq_ps(x, y); break;
    case 5: r = _mm_cmpnlt_ps(x, y); break;
    case 6: r = _mm_cmpnle_ps(x, y); break;
    default: r = _mm_cmpord_ps(x, y); break;
    }
    _mm_storeu_ps(o, r);
}
EXPORT void fp_cmppd_sel(double *o, const double *a, const double *b, u32 sel) {
    __m128d x = _mm_loadu_pd(a), y = _mm_loadu_pd(b), r;
    switch (sel & 7) {
    case 0: r = _mm_cmpeq_pd(x, y); break;
    case 1: r = _mm_cmplt_pd(x, y); break;
    case 2: r = _mm_cmple_pd(x, y); break;
    case 3: r = _mm_cmpunord_pd(x, y); break;
    case 4: r = _mm_cmpneq_pd(x, y); break;
    case 5: r = _mm_cmpnlt_pd(x, y); break;
    case 6: r = _mm_cmpnle_pd(x, y); break;
    default: r = _mm_cmpord_pd(x, y); break;
    }
    _mm_storeu_pd(o, r);
}
EXPORT void fp_cmpltps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_cmplt_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
EXPORT void fp_cmpnleps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_cmpnle_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS2)));
}
EXPORT void fp_cmpunordps_k(float *o, const float *a) {
    _mm_storeu_ps(o, _mm_cmpunord_ps(_mm_loadu_ps(a), _mm_load_ps(K_PS)));
}
EXPORT void fp_cmpltpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_cmplt_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD)));
}
EXPORT void fp_cmpnltpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_cmpnlt_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD2)));
}
EXPORT void fp_cmpneqpd_k(double *o, const double *a) {
    _mm_storeu_pd(o, _mm_cmpneq_pd(_mm_loadu_pd(a), _mm_load_pd(K_PD)));
}
