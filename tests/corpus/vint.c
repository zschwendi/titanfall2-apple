/* Integer SIMD: wrapping and saturating add/sub, multiplies, avg/min/max/sad, compares,
   shifts, packs and logic. Each operation gets one function per operand form (register,
   memory through a pointer argument, constant in read-only data; for shifts also immediate,
   out-of-range immediate, register count and memory count). Every function stores its
   128-bit result through its first pointer argument. Memory operands are masked to their
   16-byte block so the aligned loads cannot fault on alignment. */
#include "corpus.h"
#include <emmintrin.h>
#include <tmmintrin.h>
#include <smmintrin.h>

#define AL(p) ((usize)(p) & ~(usize)15)
#define LD(p) _mm_load_si128((const __m128i *)AL(p))
#define ST(p, v) _mm_store_si128((__m128i *)AL(p), (v))
#define SW(t) _mm_shuffle_epi32((t), 0x4E)

static const u8 vi_kb0[16] __attribute__((aligned(16))) = {0x01, 0x7F, 0x80, 0xFF, 0x10, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x00, 0x55, 0xAA, 0x3C};
static const u16 vi_kw0[8] __attribute__((aligned(16))) = {0x7FFF, 0x8000, 0xFFFF, 3, 0x1234, 0xFEDC, 0, 0x4000};
static const u32 vi_kd0[4] __attribute__((aligned(16))) = {0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 12345};
static const u64 vi_kq0[2] __attribute__((aligned(16))) = {0x0123456789ABCDEFull, 0xFEDCBA9876543210ull};

/* One binary operation in three operand forms: register, memory through a pointer argument,
   and a constant in read-only data. ATTR enables a target feature for the SSE4.1 and SSSE3 ops. */
#define BIN3(name, op, K, ATTR)                                                 \
EXPORT ATTR void vi_##name##_reg(u8 *o, const u8 *a) {                          \
    __m128i t = LD(a); ST(o, op(t, SW(t))); }                                   \
EXPORT ATTR void vi_##name##_mem(u8 *o, const u8 *a, const u8 *b) {             \
    ST(o, op(LD(a), LD(b))); }                                                  \
EXPORT ATTR void vi_##name##_const(u8 *o, const u8 *a) {                        \
    ST(o, op(LD(a), LD(K))); }

/* Subtraction from a constant: clang rewrites x - K as x + (-K), so the constant-pool form
   is written in inline assembly to keep psub in the output. */
#define SUB3(name, op, K)                                                       \
EXPORT void vi_##name##_reg(u8 *o, const u8 *a) {                               \
    __m128i t = LD(a); ST(o, op(t, SW(t))); }                                   \
EXPORT void vi_##name##_mem(u8 *o, const u8 *a, const u8 *b) {                  \
    ST(o, op(LD(a), LD(b))); }                                                  \
EXPORT void vi_##name##_const(u8 *o, const u8 *a) {                             \
    __m128i t = LD(a); __asm__(#name " %1, %0" : "+x"(t) : "m"(*(const __m128i *)(K))); ST(o, t); }

/* Logic operations. A bare memory or constant operand is often encoded as andps, orps or xorps,
   so those two forms feed an integer add to keep the integer encodings. pandn is ~a & b. */
#define LOG3(name, op)                                                          \
EXPORT void vi_##name##_reg(u8 *o, const u8 *a) {                               \
    __m128i t = LD(a); ST(o, op(t, SW(t))); }                                   \
EXPORT void vi_##name##_mem(u8 *o, const u8 *a, const u8 *b) {                  \
    ST(o, _mm_add_epi8(op(LD(a), LD(b)), LD(vi_kb0))); }                        \
EXPORT void vi_##name##_const(u8 *o, const u8 *a) {                             \
    ST(o, _mm_add_epi8(op(LD(a), LD(vi_kb0)), LD(vi_kb0))); }

/* Shifts: an immediate inside the lane width, an immediate at or above it (inline assembly,
   because the intrinsics fold those to constants), a count in a general register (unmasked, so
   counts at and above the width are exercised), and a count from a 128-bit memory operand. */
#define SHIFT4(name, imm_op, cnt_op, IMM, WIDE, asmop)                          \
EXPORT void vi_##name##_imm(u8 *o, const u8 *a) {                               \
    ST(o, imm_op(LD(a), IMM)); }                                                \
EXPORT void vi_##name##_wide(u8 *o, const u8 *a) {                              \
    __m128i t = LD(a); __asm__(#asmop " $" #WIDE ", %0" : "+x"(t)); ST(o, t); } \
EXPORT void vi_##name##_cnt(u8 *o, const u8 *a, u32 n) {                        \
    ST(o, cnt_op(LD(a), _mm_cvtsi32_si128((int)n))); }                          \
EXPORT void vi_##name##_cmem(u8 *o, const u8 *a, const u8 *c) {                 \
    ST(o, cnt_op(LD(a), LD(c))); }

/* Wrapping add and subtract. */
BIN3(paddb, _mm_add_epi8, vi_kb0, )
BIN3(paddw, _mm_add_epi16, vi_kw0, )
BIN3(paddd, _mm_add_epi32, vi_kd0, )
BIN3(paddq, _mm_add_epi64, vi_kq0, )
SUB3(psubb, _mm_sub_epi8, vi_kb0)
SUB3(psubw, _mm_sub_epi16, vi_kw0)
SUB3(psubd, _mm_sub_epi32, vi_kd0)
SUB3(psubq, _mm_sub_epi64, vi_kq0)

/* Saturating add and subtract. */
BIN3(paddsb, _mm_adds_epi8, vi_kb0, )
BIN3(paddsw, _mm_adds_epi16, vi_kw0, )
BIN3(paddusb, _mm_adds_epu8, vi_kb0, )
BIN3(paddusw, _mm_adds_epu16, vi_kw0, )
BIN3(psubsb, _mm_subs_epi8, vi_kb0, )
BIN3(psubsw, _mm_subs_epi16, vi_kw0, )
BIN3(psubusb, _mm_subs_epu8, vi_kb0, )
BIN3(psubusw, _mm_subs_epu16, vi_kw0, )

/* Multiplies. */
BIN3(pmullw, _mm_mullo_epi16, vi_kw0, )
BIN3(pmulhw, _mm_mulhi_epi16, vi_kw0, )
BIN3(pmulhuw, _mm_mulhi_epu16, vi_kw0, )
BIN3(pmuludq, _mm_mul_epu32, vi_kq0, )
BIN3(pmaddwd, _mm_madd_epi16, vi_kw0, )
BIN3(pmulld, _mm_mullo_epi32, vi_kd0, __attribute__((target("sse4.1"))))
BIN3(pmaddubsw, _mm_maddubs_epi16, vi_kb0, __attribute__((target("ssse3"))))

/* Average, min, max, and sum of absolute byte differences. */
BIN3(pavgb, _mm_avg_epu8, vi_kb0, )
BIN3(pavgw, _mm_avg_epu16, vi_kw0, )
BIN3(pminsw, _mm_min_epi16, vi_kw0, )
BIN3(pmaxsw, _mm_max_epi16, vi_kw0, )
BIN3(pminub, _mm_min_epu8, vi_kb0, )
BIN3(pmaxub, _mm_max_epu8, vi_kb0, )
BIN3(pminsd, _mm_min_epi32, vi_kd0, __attribute__((target("sse4.1"))))
BIN3(psadbw, _mm_sad_epu8, vi_kb0, )

/* Equality and signed greater-than compares; result lanes are all ones or all zeros. */
BIN3(pcmpeqb, _mm_cmpeq_epi8, vi_kb0, )
BIN3(pcmpeqw, _mm_cmpeq_epi16, vi_kw0, )
BIN3(pcmpeqd, _mm_cmpeq_epi32, vi_kd0, )
BIN3(pcmpgtb, _mm_cmpgt_epi8, vi_kb0, )
BIN3(pcmpgtw, _mm_cmpgt_epi16, vi_kw0, )
BIN3(pcmpgtd, _mm_cmpgt_epi32, vi_kd0, )

/* Pack with signed or unsigned saturation. */
BIN3(packsswb, _mm_packs_epi16, vi_kb0, )
BIN3(packssdw, _mm_packs_epi32, vi_kw0, )
BIN3(packuswb, _mm_packus_epi16, vi_kb0, )

/* Bitwise logic. */
LOG3(pand, _mm_and_si128)
LOG3(pandn, _mm_andnot_si128)
LOG3(por, _mm_or_si128)
LOG3(pxor, _mm_xor_si128)

/* Shifts, arguments: immediate intrinsic, count intrinsic, in-range count, out-of-range count,
   instruction name. */
SHIFT4(psllw, _mm_slli_epi16, _mm_sll_epi16, 3, 16, psllw)
SHIFT4(pslld, _mm_slli_epi32, _mm_sll_epi32, 5, 40, pslld)
SHIFT4(psllq, _mm_slli_epi64, _mm_sll_epi64, 37, 64, psllq)
SHIFT4(psrlw, _mm_srli_epi16, _mm_srl_epi16, 4, 20, psrlw)
SHIFT4(psrld, _mm_srli_epi32, _mm_srl_epi32, 9, 32, psrld)
SHIFT4(psrlq, _mm_srli_epi64, _mm_srl_epi64, 33, 255, psrlq)
SHIFT4(psraw, _mm_srai_epi16, _mm_sra_epi16, 2, 16, psraw)
SHIFT4(psrad, _mm_srai_epi32, _mm_sra_epi32, 13, 40, psrad)
