/* Vector shuffles, unpacks, partial moves, byte shifts, inserts, extracts and non-temporal
   stores.  Register forms take a 128-bit value as two u64 halves and return a fold of the
   result.  Memory forms take pointers; the 16-byte access is at the pointer rounded up to a
   multiple of 16, so aligned SSE memory operands and non-temporal stores are legal. */
#include "corpus.h"
#include <smmintrin.h>

#define ALWAYS static __attribute__((always_inline)) inline
#define SSE3 __attribute__((target("sse3")))
#define SSSE3 __attribute__((target("ssse3")))
#define SSE41 __attribute__((target("sse4.1")))

/* Opaque barrier: the value must be produced in an xmm register, so the compiler cannot
   simplify the operation that produced it into scalar arithmetic. */
ALWAYS __m128i kv(__m128i v) { __asm__("" : "+x"(v)); return v; }
ALWAYS __m128i v2(u64 lo, u64 hi) { return kv(_mm_set_epi64x((long long)hi, (long long)lo)); }
ALWAYS __m128 vf(u64 lo, u64 hi) { return _mm_castsi128_ps(v2(lo, hi)); }
ALWAYS __m128d vd(u64 lo, u64 hi) { return _mm_castsi128_pd(v2(lo, hi)); }
ALWAYS u64 fold(__m128i v) {
    v = kv(v);
    return (u64)_mm_cvtsi128_si64(v) * 0x9E3779B97F4A7C15ULL + (u64)_mm_cvtsi128_si64(_mm_srli_si128(v, 8));
}
ALWAYS u64 foldf(__m128 v) { return fold(_mm_castps_si128(v)); }
ALWAYS u64 foldd(__m128d v) { return fold(_mm_castpd_si128(v)); }
ALWAYS u8 *up16(const u8 *p) { return (u8 *)(((usize)p + 15) & ~(usize)15); }
ALWAYS __m128i ldi(const u8 *p) { return *(const __m128i *)up16(p); }
ALWAYS __m128i ldo(const u8 *p) { return kv(ldi(p)); }
ALWAYS __m128 ldf(const u8 *p) { return _mm_castsi128_ps(ldi(p)); }
ALWAYS __m128d ldd(const u8 *p) { return _mm_castsi128_pd(ldi(p)); }
ALWAYS void sti(u8 *p, __m128i v) { _mm_store_si128((__m128i *)up16(p), v); }
ALWAYS void stf(u8 *p, __m128 v) { sti(p, _mm_castps_si128(v)); }
ALWAYS void stdp(u8 *p, __m128d v) { sti(p, _mm_castpd_si128(v)); }

/* Read-only constants.  Aligned so that they can be used as SSE memory operands. */
static const u8 k_f32[16] __attribute__((aligned(16))) = {0, 0, 0x80, 0x3F, 0, 0, 0x20, 0x40, 0, 0, 0x40, 0xC0, 0, 0, 0, 0x3F};
static const u8 k_f64[16] __attribute__((aligned(16))) = {0, 0, 0, 0, 0, 0, 0xF0, 0x3F, 0, 0, 0, 0, 0, 0, 0x08, 0x40};
static const u8 k_int[16] __attribute__((aligned(16))) = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const u8 k_rev[16] __attribute__((aligned(16))) = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const u8 k_sel[16] __attribute__((aligned(16))) = {0x80, 0, 0x80, 1, 0x80, 2, 0x80, 3, 4, 0x80, 5, 0x80, 6, 0x80, 7, 0x80};

/* ---- shufps ---- */
EXPORT u64 vs_shufps_rr_1b(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0x1B)); }
EXPORT u64 vs_shufps_rr_4e(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0x4E)); }
EXPORT u64 vs_shufps_rr_b1(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0xB1)); }
EXPORT u64 vs_shufps_rr_39(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0x39)); }
EXPORT u64 vs_shufps_rr_93(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0x93)); }
EXPORT u64 vs_shufps_rr_27(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0x27)); }
EXPORT u64 vs_shufps_rr_c6(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0xC6)); }
EXPORT u64 vs_shufps_rr_00(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_shuffle_ps(vf(al, ah), vf(bl, bh), 0x00)); }
EXPORT u64 vs_shufps_self_8d(u64 lo, u64 hi) { __m128 v = vf(lo, hi); return foldf(_mm_shuffle_ps(v, v, 0x8D)); }
EXPORT void vs_shufps_m_0f(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_shuffle_ps(ldf(s), ldf(t), 0x0F)); }
EXPORT void vs_shufps_m_f0(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_shuffle_ps(ldf(s), ldf(t), 0xF0)); }
EXPORT void vs_shufps_m_d8(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_shuffle_ps(ldf(s), ldf(t), 0xD8)); }
EXPORT void vs_shufps_m_e4(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_shuffle_ps(ldf(s), ldf(t), 0xE4)); }
EXPORT u64 vs_shufps_k_2d(u64 lo, u64 hi) { return foldf(_mm_shuffle_ps(vf(lo, hi), ldf(k_f32), 0x2D)); }

/* ---- shufpd ---- */
EXPORT u64 vs_shufpd_rr_0(u64 al, u64 ah, u64 bl, u64 bh) { return foldd(_mm_shuffle_pd(vd(al, ah), vd(bl, bh), 0)); }
EXPORT u64 vs_shufpd_rr_1(u64 al, u64 ah, u64 bl, u64 bh) { return foldd(_mm_shuffle_pd(vd(al, ah), vd(bl, bh), 1)); }
EXPORT u64 vs_shufpd_rr_2(u64 al, u64 ah, u64 bl, u64 bh) { return foldd(_mm_shuffle_pd(vd(al, ah), vd(bl, bh), 2)); }
EXPORT u64 vs_shufpd_rr_3(u64 al, u64 ah, u64 bl, u64 bh) { return foldd(_mm_shuffle_pd(vd(al, ah), vd(bl, bh), 3)); }
EXPORT void vs_shufpd_m_1(u8 *d, const u8 *s, const u8 *t) { stdp(d, _mm_shuffle_pd(ldd(s), ldd(t), 1)); }
EXPORT void vs_shufpd_m_2(u8 *d, const u8 *s, const u8 *t) { stdp(d, _mm_shuffle_pd(ldd(s), ldd(t), 2)); }
EXPORT u64 vs_shufpd_k_1(u64 lo, u64 hi) { return foldd(_mm_shuffle_pd(vd(lo, hi), ldd(k_f64), 1)); }

/* ---- unpcklps / unpckhps / unpcklpd / unpckhpd ---- */
EXPORT u64 vs_unpcklps_rr(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_unpacklo_ps(vf(al, ah), vf(bl, bh))); }
EXPORT u64 vs_unpckhps_rr(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_unpackhi_ps(vf(al, ah), vf(bl, bh))); }
EXPORT void vs_unpcklps_m(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_unpacklo_ps(ldf(s), ldf(t))); }
EXPORT void vs_unpckhps_m(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_unpackhi_ps(ldf(s), ldf(t))); }
EXPORT u64 vs_unpcklps_k(u64 lo, u64 hi) { return foldf(_mm_unpacklo_ps(vf(lo, hi), ldf(k_f32))); }
EXPORT u64 vs_unpcklpd_rr(u64 al, u64 ah, u64 bl, u64 bh) { return foldd(_mm_unpacklo_pd(vd(al, ah), vd(bl, bh))); }
EXPORT u64 vs_unpckhpd_rr(u64 al, u64 ah, u64 bl, u64 bh) { return foldd(_mm_unpackhi_pd(vd(al, ah), vd(bl, bh))); }
EXPORT void vs_unpcklpd_m(u8 *d, const u8 *s, const u8 *t) { stdp(d, _mm_unpacklo_pd(ldd(s), ldd(t))); }
EXPORT void vs_unpckhpd_m(u8 *d, const u8 *s, const u8 *t) { stdp(d, _mm_unpackhi_pd(ldd(s), ldd(t))); }
EXPORT u64 vs_unpckhpd_k(u64 lo, u64 hi) { return foldd(_mm_unpackhi_pd(vd(lo, hi), ldd(k_f64))); }

/* ---- movlhps / movhlps ---- */
EXPORT u64 vs_movlhps_rr(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_movelh_ps(vf(al, ah), vf(bl, bh))); }
EXPORT u64 vs_movhlps_rr(u64 al, u64 ah, u64 bl, u64 bh) { return foldf(_mm_movehl_ps(vf(al, ah), vf(bl, bh))); }
EXPORT void vs_movlhps_m(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_movelh_ps(_mm_castsi128_ps(ldo(s)), _mm_castsi128_ps(ldo(t)))); }
EXPORT void vs_movhlps_m(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_movehl_ps(_mm_castsi128_ps(ldo(s)), _mm_castsi128_ps(ldo(t)))); }
EXPORT u64 vs_movhlps_k(u64 lo, u64 hi) { return foldf(_mm_movehl_ps(vf(lo, hi), ldf(k_f32))); }

/* ---- movhps / movlps / movhpd / movlpd: 64-bit half loads and stores ---- */
EXPORT void vs_movhps_ld(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_loadh_pi(ldf(s), (const __m64 *)t)); }
EXPORT void vs_movhps_st(u8 *d, const u8 *s) { _mm_storeh_pi((__m64 *)d, ldf(s)); }
EXPORT u64 vs_movhps_k(u64 lo, u64 hi) { return foldf(_mm_loadh_pi(vf(lo, hi), (const __m64 *)k_f32)); }
EXPORT void vs_movlps_ld(u8 *d, const u8 *s, const u8 *t) { stf(d, _mm_loadl_pi(ldf(s), (const __m64 *)t)); }
EXPORT void vs_movlps_st(u8 *d, const u8 *s) { _mm_storel_pi((__m64 *)d, ldf(s)); }
EXPORT u64 vs_movlps_k(u64 lo, u64 hi) { return foldf(_mm_loadl_pi(vf(lo, hi), (const __m64 *)k_f32)); }

/* The compiler prefers movhps/movlps and movsd for these, so the movhpd/movlpd
   encodings are written out in inline assembly. */
EXPORT void vs_movhpd_ld(u8 *d, const u8 *s, const u8 *t) {
    __m128d v = ldd(s);
    __asm__("movhpd %1, %0" : "+x"(v) : "m"(*(const double *)t));
    stdp(d, v);
}
EXPORT void vs_movhpd_st(u8 *d, const u8 *s) {
    __m128d v = kv(_mm_castsi128_pd(ldo(s)));
    __asm__("movhpd %1, %0" : "=m"(*(double *)d) : "x"(v));
}
EXPORT u64 vs_movhpd_k(u64 lo, u64 hi) {
    __m128d v = vd(lo, hi);
    __asm__("movhpd %1, %0" : "+x"(v) : "m"(*(const double *)k_f64));
    return foldd(v);
}
EXPORT void vs_movlpd_ld(u8 *d, const u8 *s, const u8 *t) {
    __m128d v = ldd(s);
    __asm__("movlpd %1, %0" : "+x"(v) : "m"(*(const double *)t));
    stdp(d, v);
}
EXPORT void vs_movlpd_st(u8 *d, const u8 *s) {
    __m128d v = kv(_mm_castsi128_pd(ldo(s)));
    __asm__("movlpd %1, %0" : "=m"(*(double *)d) : "x"(v));
}
EXPORT u64 vs_movlpd_k(u64 lo, u64 hi) {
    __m128d v = vd(lo, hi);
    __asm__("movlpd %1, %0" : "+x"(v) : "m"(*(const double *)k_f64));
    return foldd(v);
}

/* ---- SSE3 duplicates ---- */
EXPORT SSE3 u64 vs_movddup_rr(u64 lo, u64 hi) { return foldd(_mm_movedup_pd(vd(lo, hi))); }
EXPORT SSE3 void vs_movddup_m(u8 *d, const u8 *s) { stdp(d, _mm_movedup_pd(ldd(s))); }
EXPORT SSE3 void vs_loaddup_m(u8 *d, const u8 *s) { stdp(d, _mm_loaddup_pd((const double *)s)); }
EXPORT SSE3 u64 vs_movsldup_rr(u64 lo, u64 hi) { return foldf(_mm_moveldup_ps(vf(lo, hi))); }
EXPORT SSE3 void vs_movsldup_m(u8 *d, const u8 *s) { stf(d, _mm_moveldup_ps(ldf(s))); }
EXPORT SSE3 u64 vs_movshdup_rr(u64 lo, u64 hi) { return foldf(_mm_movehdup_ps(vf(lo, hi))); }
EXPORT SSE3 void vs_movshdup_m(u8 *d, const u8 *s) { stf(d, _mm_movehdup_ps(ldf(s))); }

/* ---- pshufd ---- */
EXPORT u64 vs_pshufd_rr_1b(u64 lo, u64 hi) { return fold(_mm_shuffle_epi32(v2(lo, hi), 0x1B)); }
EXPORT u64 vs_pshufd_rr_4e(u64 lo, u64 hi) { return fold(_mm_shuffle_epi32(v2(lo, hi), 0x4E)); }
EXPORT u64 vs_pshufd_rr_93(u64 lo, u64 hi) { return fold(_mm_shuffle_epi32(v2(lo, hi), 0x93)); }
EXPORT u64 vs_pshufd_rr_00(u64 lo, u64 hi) { return fold(_mm_shuffle_epi32(v2(lo, hi), 0x00)); }
EXPORT u64 vs_pshufd_rr_ff(u64 lo, u64 hi) { return fold(_mm_shuffle_epi32(v2(lo, hi), 0xFF)); }
EXPORT u64 vs_pshufd_rr_55(u64 lo, u64 hi) { return fold(_mm_shuffle_epi32(v2(lo, hi), 0x55)); }
EXPORT void vs_pshufd_m_e4(u8 *d, const u8 *s) { sti(d, _mm_shuffle_epi32(ldi(s), 0xE4)); }
EXPORT void vs_pshufd_m_6c(u8 *d, const u8 *s) { sti(d, _mm_shuffle_epi32(ldi(s), 0x6C)); }
EXPORT void vs_pshufd_m_b1(u8 *d, const u8 *s) { sti(d, _mm_shuffle_epi32(ldi(s), 0xB1)); }

/* ---- pshuflw / pshufhw ---- */
EXPORT u64 vs_pshuflw_rr_1b(u64 lo, u64 hi) { return fold(_mm_shufflelo_epi16(v2(lo, hi), 0x1B)); }
EXPORT u64 vs_pshuflw_rr_e4(u64 lo, u64 hi) { return fold(_mm_shufflelo_epi16(v2(lo, hi), 0xE4)); }
EXPORT u64 vs_pshuflw_rr_00(u64 lo, u64 hi) { return fold(_mm_shufflelo_epi16(v2(lo, hi), 0x00)); }
EXPORT void vs_pshuflw_m_39(u8 *d, const u8 *s) { sti(d, _mm_shufflelo_epi16(ldi(s), 0x39)); }
EXPORT void vs_pshuflw_m_c6(u8 *d, const u8 *s) { sti(d, _mm_shufflelo_epi16(ldi(s), 0xC6)); }
EXPORT u64 vs_pshufhw_rr_b1(u64 lo, u64 hi) { return fold(_mm_shufflehi_epi16(v2(lo, hi), 0xB1)); }
EXPORT u64 vs_pshufhw_rr_4e(u64 lo, u64 hi) { return fold(_mm_shufflehi_epi16(v2(lo, hi), 0x4E)); }
EXPORT void vs_pshufhw_m_27(u8 *d, const u8 *s) { sti(d, _mm_shufflehi_epi16(ldi(s), 0x27)); }
EXPORT void vs_pshufhw_m_6c(u8 *d, const u8 *s) { sti(d, _mm_shufflehi_epi16(ldi(s), 0x6C)); }

/* ---- SSSE3: pshufb, palignr ---- */
EXPORT SSSE3 u64 vs_pshufb_rr(u64 alo, u64 ahi, u64 mlo, u64 mhi) { return fold(_mm_shuffle_epi8(v2(alo, ahi), v2(mlo, mhi))); }
EXPORT SSSE3 void vs_pshufb_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_shuffle_epi8(ldi(s), ldi(t))); }
EXPORT SSSE3 u64 vs_pshufb_k_rev(u64 lo, u64 hi) { return fold(_mm_shuffle_epi8(v2(lo, hi), ldi(k_rev))); }
EXPORT SSSE3 u64 vs_pshufb_k_sel(u64 lo, u64 hi) { return fold(_mm_shuffle_epi8(ldi(k_int), v2(lo, hi))); }
EXPORT SSSE3 u64 vs_palignr_rr_04(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_alignr_epi8(v2(alo, ahi), v2(blo, bhi), 4)); }
EXPORT SSSE3 u64 vs_palignr_rr_08(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_alignr_epi8(v2(alo, ahi), v2(blo, bhi), 8)); }
EXPORT SSSE3 u64 vs_palignr_rr_0d(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_alignr_epi8(v2(alo, ahi), v2(blo, bhi), 13)); }
EXPORT SSSE3 void vs_palignr_m_01(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_alignr_epi8(ldi(s), ldi(t), 1)); }
EXPORT SSSE3 void vs_palignr_m_07(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_alignr_epi8(ldi(s), ldi(t), 7)); }
EXPORT SSSE3 void vs_palignr_m_0f(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_alignr_epi8(ldi(s), ldi(t), 15)); }
EXPORT SSSE3 u64 vs_palignr_k_03(u64 lo, u64 hi) { return fold(_mm_alignr_epi8(v2(lo, hi), ldi(k_int), 3)); }

/* ---- byte shifts ---- */
EXPORT u64 vs_pslldq_rr_01(u64 lo, u64 hi) { return fold(_mm_slli_si128(v2(lo, hi), 1)); }
EXPORT u64 vs_pslldq_rr_03(u64 lo, u64 hi) { return fold(_mm_slli_si128(v2(lo, hi), 3)); }
EXPORT u64 vs_pslldq_rr_08(u64 lo, u64 hi) { return fold(_mm_slli_si128(v2(lo, hi), 8)); }
EXPORT u64 vs_pslldq_rr_0f(u64 lo, u64 hi) { return fold(_mm_slli_si128(v2(lo, hi), 15)); }
EXPORT void vs_pslldq_m_05(u8 *d, const u8 *s) { sti(d, _mm_slli_si128(ldi(s), 5)); }
EXPORT void vs_pslldq_m_0c(u8 *d, const u8 *s) { sti(d, _mm_slli_si128(ldi(s), 12)); }
EXPORT u64 vs_psrldq_rr_01(u64 lo, u64 hi) { return fold(_mm_srli_si128(v2(lo, hi), 1)); }
EXPORT u64 vs_psrldq_rr_04(u64 lo, u64 hi) { return fold(_mm_srli_si128(v2(lo, hi), 4)); }
EXPORT u64 vs_psrldq_rr_07(u64 lo, u64 hi) { return fold(_mm_srli_si128(v2(lo, hi), 7)); }
EXPORT u64 vs_psrldq_rr_0e(u64 lo, u64 hi) { return fold(_mm_srli_si128(v2(lo, hi), 14)); }
EXPORT void vs_psrldq_m_02(u8 *d, const u8 *s) { sti(d, _mm_srli_si128(ldi(s), 2)); }
EXPORT void vs_psrldq_m_0b(u8 *d, const u8 *s) { sti(d, _mm_srli_si128(ldi(s), 11)); }

/* ---- punpck{l,h}{bw,wd,dq,qdq} ---- */
EXPORT u64 vs_punpcklbw_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_unpacklo_epi8(v2(alo, ahi), v2(blo, bhi))); }
EXPORT void vs_punpcklbw_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_unpacklo_epi8(ldi(s), ldi(t))); }
EXPORT u64 vs_punpckhbw_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_unpackhi_epi8(v2(alo, ahi), v2(blo, bhi))); }
EXPORT void vs_punpckhbw_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_unpackhi_epi8(ldi(s), ldi(t))); }
EXPORT u64 vs_punpcklwd_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_unpacklo_epi16(v2(alo, ahi), v2(blo, bhi))); }
EXPORT void vs_punpcklwd_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_unpacklo_epi16(ldi(s), ldi(t))); }
EXPORT u64 vs_punpckhwd_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_unpackhi_epi16(v2(alo, ahi), v2(blo, bhi))); }
EXPORT void vs_punpckhwd_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_unpackhi_epi16(ldi(s), ldi(t))); }
/* The compiler turns integer 32-bit unpacks into unpcklps/unpckhps, so these are inline asm. */
EXPORT u64 vs_punpckldq_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) {
    __m128i a = v2(alo, ahi);
    __asm__("punpckldq %1, %0" : "+x"(a) : "x"(v2(blo, bhi)));
    return fold(a);
}
EXPORT void vs_punpckldq_m(u8 *d, const u8 *s, const u8 *t) {
    __m128i a = ldo(s);
    __asm__("punpckldq %1, %0" : "+x"(a) : "m"(*(const __m128i *)up16(t)));
    sti(d, a);
}
EXPORT u64 vs_punpckhdq_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) {
    __m128i a = v2(alo, ahi);
    __asm__("punpckhdq %1, %0" : "+x"(a) : "x"(v2(blo, bhi)));
    return fold(a);
}
EXPORT void vs_punpckhdq_m(u8 *d, const u8 *s, const u8 *t) {
    __m128i a = ldo(s);
    __asm__("punpckhdq %1, %0" : "+x"(a) : "m"(*(const __m128i *)up16(t)));
    sti(d, a);
}
EXPORT u64 vs_punpcklqdq_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_unpacklo_epi64(v2(alo, ahi), v2(blo, bhi))); }
EXPORT void vs_punpcklqdq_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_unpacklo_epi64(ldi(s), ldi(t))); }
EXPORT u64 vs_punpckhqdq_rr(u64 alo, u64 ahi, u64 blo, u64 bhi) { return fold(_mm_unpackhi_epi64(v2(alo, ahi), v2(blo, bhi))); }
EXPORT void vs_punpckhqdq_m(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_unpackhi_epi64(ldi(s), ldi(t))); }

/* ---- pinsrw / pextrw (SSE2) ---- */
EXPORT u64 vs_pinsrw_rr_3(u64 lo, u64 hi, u32 x) { return fold(_mm_insert_epi16(v2(lo, hi), (int)x, 3)); }
EXPORT u64 vs_pinsrw_rr_7(u64 lo, u64 hi, u32 x) { return fold(_mm_insert_epi16(v2(lo, hi), (int)x, 7)); }
EXPORT void vs_pinsrw_m_5(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_insert_epi16(ldi(s), (int)*(const u16 *)t, 5)); }
EXPORT void vs_pinsrw_m_0(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_insert_epi16(ldi(s), (int)*(const u16 *)t, 0)); }
EXPORT u32 vs_pextrw_rr_2(u64 lo, u64 hi) { return (u32)_mm_extract_epi16(v2(lo, hi), 2); }
EXPORT u32 vs_pextrw_rr_6(u64 lo, u64 hi) { return (u32)_mm_extract_epi16(v2(lo, hi), 6); }
EXPORT void vs_pextrw_m_4(u8 *d, const u8 *s) { *(u16 *)d = (u16)_mm_extract_epi16(ldo(s), 4); }

/* ---- SSE4.1: pinsrb, pinsrd, pextrd ---- */
EXPORT SSE41 u64 vs_pinsrb_rr_9(u64 lo, u64 hi, u32 x) { return fold(_mm_insert_epi8(v2(lo, hi), (int)x, 9)); }
EXPORT SSE41 u64 vs_pinsrb_rr_f(u64 lo, u64 hi, u32 x) { return fold(_mm_insert_epi8(v2(lo, hi), (int)x, 15)); }
EXPORT SSE41 void vs_pinsrb_m_c(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_insert_epi8(ldi(s), (int)*t, 12)); }
EXPORT SSE41 void vs_pinsrb_m_3(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_insert_epi8(ldi(s), (int)*t, 3)); }
EXPORT SSE41 u64 vs_pinsrd_rr_1(u64 lo, u64 hi, u32 x) { return fold(_mm_insert_epi32(v2(lo, hi), (int)x, 1)); }
EXPORT SSE41 u64 vs_pinsrd_rr_3(u64 lo, u64 hi, u32 x) { return fold(_mm_insert_epi32(v2(lo, hi), (int)x, 3)); }
EXPORT SSE41 void vs_pinsrd_m_2(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_insert_epi32(ldi(s), (int)*(const u32 *)t, 2)); }
EXPORT SSE41 void vs_pinsrd_m_0(u8 *d, const u8 *s, const u8 *t) { sti(d, _mm_insert_epi32(ldi(s), (int)*(const u32 *)t, 0)); }
/* The compiler emits extractps for lane extracts, so pextrd is written in inline asm. */
EXPORT SSE41 u32 vs_pextrd_rr_2(u64 lo, u64 hi) {
    u32 r;
    __asm__("pextrd %2, %1, %0" : "=r"(r) : "x"(v2(lo, hi)), "i"(2));
    return r;
}
EXPORT SSE41 u32 vs_pextrd_rr_1(u64 lo, u64 hi) {
    u32 r;
    __asm__("pextrd %2, %1, %0" : "=r"(r) : "x"(v2(lo, hi)), "i"(1));
    return r;
}
EXPORT SSE41 void vs_pextrd_m_3(u8 *d, const u8 *s) {
    __m128i v = ldo(s);
    __asm__("pextrd %2, %1, %0" : "=m"(*(u32 *)d) : "x"(v), "i"(3));
}
EXPORT SSE41 void vs_pextrd_m_2(u8 *d, const u8 *s) {
    __m128i v = ldo(s);
    __asm__("pextrd %2, %1, %0" : "=m"(*(u32 *)d) : "x"(v), "i"(2));
}

/* ---- non-temporal stores ---- */
EXPORT void vs_movnti32_rr(u8 *d, u32 x) { _mm_stream_si32((int *)d, (int)x); }
EXPORT void vs_movnti64_rr(u8 *d, u64 x) { _mm_stream_si64((long long *)d, (long long)x); }
EXPORT void vs_movnti32_m(u8 *d, const u8 *s) { _mm_stream_si32((int *)d, *(const int *)s); }
EXPORT void vs_movntps_rr(u8 *d, u64 lo, u64 hi) { _mm_stream_ps((float *)up16(d), vf(lo, hi)); }
EXPORT void vs_movntps_m(u8 *d, const u8 *s) { _mm_stream_ps((float *)up16(d), ldf(s)); }
/* LLVM emits movntps for integer non-temporal stores, so movntdq is written in inline asm. */
EXPORT void vs_movntdq_rr(u8 *d, u64 lo, u64 hi) { __asm__("movntdq %0, (%1)" : : "x"(v2(lo, hi)), "r"(up16(d)) : "memory"); }
EXPORT void vs_movntdq_m(u8 *d, const u8 *s) { __asm__("movntdq %0, (%1)" : : "x"(ldo(s)), "r"(up16(d)) : "memory"); }

/* ---- lddqu (SSE3): unaligned load ---- */
EXPORT SSE3 void vs_lddqu_m(u8 *d, const u8 *s) { sti(d, _mm_lddqu_si128((const __m128i *)s)); }
EXPORT SSE3 void vs_lddqu_off_m(u8 *d, const u8 *s, u32 n) { sti(d, _mm_lddqu_si128((const __m128i *)(s + (n & 15)))); }

/* ---- pmovmskb ---- */
EXPORT u32 vs_pmovmskb_rr(u64 lo, u64 hi) { return (u32)_mm_movemask_epi8(v2(lo, hi)); }
EXPORT u32 vs_pmovmskb_m(const u8 *s) { return (u32)_mm_movemask_epi8(ldi(s)); }
EXPORT u32 vs_pmovmskb_k(u64 lo, u64 hi) { return (u32)_mm_movemask_epi8(_mm_xor_si128(v2(lo, hi), ldi(k_sel))); }
