/* Stand-in for <emmintrin.h> when tests/corpus/abi.c is built natively for arm64: the real
 * header refuses to compile for a non-x86 target.  Only the nine intrinsics abi.c uses, as
 * compiler vector extensions.  The functions that use them (ab_vscale4, ab_vsum_blocks,
 * ab_vlocal) are float code the lifter does not translate yet, so they are not compared;
 * this exists so that the rest of the file builds. */
#ifndef BENCH_NATIVE_EMMINTRIN_H
#define BENCH_NATIVE_EMMINTRIN_H
typedef float __m128 __attribute__((vector_size(16), aligned(16)));
typedef float __m128_u __attribute__((vector_size(16), aligned(1)));
#define BN_INL static inline __attribute__((always_inline, unused))
BN_INL __m128 _mm_loadu_ps(const float *p) { return *(const __m128_u *)p; }
BN_INL void _mm_storeu_ps(float *p, __m128 v) { *(__m128_u *)p = v; }
BN_INL void _mm_store_ps(float *p, __m128 v) { *(__m128 *)p = v; }
BN_INL __m128 _mm_mul_ps(__m128 a, __m128 b) { return a * b; }
BN_INL __m128 _mm_add_ps(__m128 a, __m128 b) { return a + b; }
BN_INL __m128 _mm_set1_ps(float k) { return (__m128){ k, k, k, k }; }
BN_INL __m128 _mm_setzero_ps(void) { return (__m128){ 0, 0, 0, 0 }; }
BN_INL __m128 _mm_set_ps(float e3, float e2, float e1, float e0) { return (__m128){ e0, e1, e2, e3 }; }
BN_INL float _mm_cvtss_f32(__m128 v) { return v[0]; }
#endif
