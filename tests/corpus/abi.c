/* Calling-convention shapes: argument and result classes, structs of each size, variadics,
   indirect and recursive calls, tail calls, switch lowering, computed goto, 128-bit values,
   and values live across calls. Helpers marked NOINLINE stay real calls. */
#include <emmintrin.h>
#include "corpus.h"

typedef unsigned __int128 AbU128;

typedef struct { u8 v; } AbS1;
typedef struct { u8 a, b; } AbS2;
typedef struct { u8 a[3]; } AbS3;
typedef struct { u16 a, b; } AbS4;
typedef struct { float x; u32 n; } AbS8;
typedef struct { u32 a, b, c; } AbS12;
typedef struct { u64 lo, hi; } AbS16;
typedef struct { double d[3]; } AbS24;

typedef u64 (*AbOp2)(u64, u64);
typedef AbS12 (*AbMk)(u32);
typedef float (*AbFF)(float, double, u32, float, float);

/* ---- more than four arguments: stack slots, float and double classes ---- */

NOINLINE static u64 ab_h_six(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) { return a - 2 * b + 3 * c - 4 * d + 5 * e - 6 * f; }
EXPORT u64 ab_args6(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) { return ab_h_six(f, e, d, c, b, a) ^ a; }
EXPORT u64 ab_args6_pass(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) { return ab_h_six(a, b, c, d, e, f) + (e & f); }
EXPORT u64 ab_args8(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h) { return ab_h_six(a, b, c, d, e, f) * (g | 1) + h; }

NOINLINE static double ab_h_mix5(u32 a, float b, double c, u64 d, float e) { return (double)a + (double)b * c - (double)d + (double)e; }
EXPORT double ab_mix5(u32 a, float b, double c, u64 d, float e) { return ab_h_mix5(a, b, c, d, e) * 0.5; }

NOINLINE static float ab_h_mix8(u32 a, float b, double c, u64 d, float e, double f, u8 g, u16 h) {
    return (float)(a + d + g - h) + b * (float)c + e * (float)f;
}
EXPORT float ab_mix8(u32 a, float b, double c, u64 d, float e, double f, u8 g, u16 h) { return ab_h_mix8(a, b, c, d, e, f, g, h) - 1.0f; }
EXPORT float ab_mix8_rev(u16 a, u8 b, double c, float d, u64 e, double f, float g, u32 h) { return ab_h_mix8(a, d, f, e, g, c, b, (u16)h) * 0.25f; }

EXPORT float ab_fstack6(float a, float b, float c, float d, float e, float f) { return a * e - b * f + c - d; }
EXPORT double ab_dstack7(u64 a, u64 b, u64 c, u64 d, double e, double f, double g) { return (double)a * e + (double)b * f - (double)c * g + (double)d; }
EXPORT float ab_pos2_float(u64 a, float b, double c, u32 d) { return b * (float)c + (float)a - (float)d; }
EXPORT double ab_pos2_double(double a, double b, u32 c, float d, u64 e) { return a * b - (double)c * (double)d + (double)e; }
EXPORT u32 ab_args_narrow(u8 a, i16 b, u32 c, i8 d, u16 e, u8 f, i32 g) {
    return ((u32)a + (u32)(i32)b + c * (u32)(i32)d + e) ^ ((u32)f * 3u + (u32)g);
}

/* ---- structs by value: sizes 1, 2, 3, 4, 8, 12, 16, 24, as arguments and results ---- */

NOINLINE static AbS1 ab_h_s1(u32 x) { AbS1 r = { (u8)(x * 37u + 11u) }; return r; }
EXPORT u32 ab_s1_arg(AbS1 s, u32 k) { return (u32)s.v * k + 1u; }
EXPORT AbS1 ab_s1_ret(u32 x) { return ab_h_s1(x ^ 0x5au); }

EXPORT u32 ab_s2_arg(AbS2 s, AbS2 t) { return (u32)(s.a + t.b * 3u) ^ (u32)(s.b - t.a); }
EXPORT AbS2 ab_s2_ret(u32 x) { AbS2 r = { (u8)x, (u8)(x >> 8) }; return r; }

EXPORT u32 ab_s3_arg(AbS3 s, u32 k) { return s.a[0] + s.a[1] * k + s.a[2] * 7u; }
EXPORT AbS3 ab_s3_ret(u32 x) { AbS3 r = { { (u8)x, (u8)(x >> 8), (u8)(x >> 16) } }; return r; }

EXPORT u32 ab_s4_arg(AbS4 s, u32 k, AbS4 *out) { out->a = (u16)(s.a ^ k); out->b = (u16)(s.b + k); return (u32)s.a * k - s.b; }
EXPORT AbS4 ab_s4_ret(u32 x) { AbS4 r = { (u16)x, (u16)(x >> 16) }; return r; }

EXPORT float ab_s8_arg(AbS8 s, float k) { return s.x * k + (float)s.n; }
EXPORT AbS8 ab_s8_ret(float x, u32 n) { AbS8 r = { x * 0.5f, n ^ 3u }; return r; }

EXPORT u64 ab_s12_arg(AbS12 s, u64 k) { return (u64)s.a + (u64)s.b * k - (u64)s.c; }
EXPORT AbS12 ab_s12_ret(u32 x) { AbS12 r = { x, x * 3u, x ^ 0x1234u }; return r; }
EXPORT AbS12 ab_s12_swap(AbS12 s, u32 k) { AbS12 r = { s.c ^ k, s.a, s.b + k }; return r; }

EXPORT u64 ab_s16_arg(AbS16 s, u64 k) { return (s.lo * k) ^ s.hi; }
EXPORT AbS16 ab_s16_ret(u64 x) { AbS16 r = { x, ~x }; return r; }
EXPORT AbS16 ab_s16_add(AbS16 a, AbS16 b) { AbS16 r = { a.lo + b.lo, a.hi ^ b.hi }; return r; }

EXPORT double ab_s24_arg(AbS24 s, double k) { return s.d[0] + s.d[1] * k - s.d[2]; }
EXPORT AbS24 ab_s24_ret(double x) { AbS24 r = { { x, x * 2.0, -x } }; return r; }

/* A struct of each class in positions 2 to 6: some in registers, some by pointer on the stack. */
NOINLINE static u64 ab_h_smix(u32 a, AbS12 s, AbS8 t, AbS16 u, AbS3 v, AbS1 w) {
    return a + s.b - t.n + (u64)(u.lo ^ u.hi) + v.a[2] * 5u + w.v;
}
EXPORT u64 ab_smix(u32 a, AbS12 s, AbS8 t, AbS16 u, AbS3 v, AbS1 w) { return ab_h_smix(a + 1u, s, t, u, v, w) * 3u - a; }

/* ---- variadic functions (__builtin_ms_va_list) ---- */

EXPORT double ab_va_dsum(u32 n, ...) {
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, n);
    double s = 0.0;
    n &= 3;
    for (u32 i = 0; i < n; i++) s += __builtin_va_arg(ap, double);
    __builtin_ms_va_end(ap);
    return s;
}

EXPORT u64 ab_va_bits(u32 mask, ...) {
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, mask);
    u64 s = 0;
    for (u32 i = 0; i < 5; i++) {
        if (mask >> i & 1) s += __builtin_va_arg(ap, u32);
        else s ^= __builtin_va_arg(ap, u64) << 1;
    }
    __builtin_ms_va_end(ap);
    return s;
}

NOINLINE static u64 ab_h_vsum(u32 n, __builtin_ms_va_list ap) {
    u64 s = 0;
    for (u32 i = 0; i < n; i++) s = s * 31u + __builtin_va_arg(ap, u64);
    return s;
}

EXPORT u64 ab_va_fwd(u32 n, u64 first, ...) {
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, first);
    n &= 3;
    u64 s = first + ab_h_vsum(n, ap);
    __builtin_ms_va_end(ap);
    return s;
}

EXPORT u64 ab_va_many(u32 a, u32 b, u32 c, u32 d, u32 e, ...) {
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, e);
    u64 s = (u64)a * b + c * d + e;
    s ^= __builtin_va_arg(ap, u64);
    s += __builtin_va_arg(ap, u32);
    __builtin_ms_va_end(ap);
    return s;
}

EXPORT double ab_va_dcaller(double x) { return ab_va_dsum(3, x, x * 2.0, 1.5) + ab_va_dsum(1, x); }
EXPORT u64 ab_va_bits_caller(u64 x) { return ab_va_bits(0x5u, (u32)x, x, (u32)(x >> 7), x * 9u, x ^ 1u); }
EXPORT u64 ab_va_fwd_caller(u64 x) { return ab_va_fwd(3, x, x * 2u, x ^ 5u, 7ull); }
EXPORT u64 ab_va_many_caller(u64 x) { return ab_va_many((u32)x, 1, 2, 3, (u32)(x >> 4), x * 5u, 7u); }

/* ---- calls through function pointers and tables of them ---- */

NOINLINE static u64 ab_h_add(u64 a, u64 b) { return a + b; }
NOINLINE static u64 ab_h_mulodd(u64 a, u64 b) { return a * (b | 1u); }
NOINLINE static u64 ab_h_rotl(u64 a, u64 b) { b &= 63u; return (a << b) | (a >> ((64u - b) & 63u)); }
NOINLINE static u64 ab_h_minu(u64 a, u64 b) { return a < b ? a : b; }
NOINLINE static u64 ab_h_xorshr(u64 a, u64 b) { return (a ^ b) >> (b & 15u); }
static const AbOp2 kAbOps[6] = { ab_h_add, ab_h_mulodd, ab_h_rotl, ab_h_minu, ab_h_xorshr, ab_h_add };

EXPORT u64 ab_fp_table(u32 which, u64 a, u64 b) { return kAbOps[which % 6](a, b); }

EXPORT u64 ab_fp_all(u64 a, u64 b) {
    u64 acc = 0;
    for (u32 i = 0; i < 6; i++) acc = kAbOps[i](acc ^ a, b + i);
    return acc;
}

/* The pointer is called only after it is found in the table. */
EXPORT u64 ab_fp_checked(AbOp2 f, u64 a, u64 b) {
    for (u32 i = 0; i < 6; i++)
        if (kAbOps[i] == f) return f(a, b);
    return a - b;
}

NOINLINE static AbS12 ab_h_mk0(u32 x) { AbS12 r = { x, x * 3u, ~x }; return r; }
NOINLINE static AbS12 ab_h_mk1(u32 x) { AbS12 r = { x ^ 9u, x >> 1, x + 77u }; return r; }
static const AbMk kAbMk[2] = { ab_h_mk0, ab_h_mk1 };
EXPORT u64 ab_fp_sret(u32 which, u32 x) { AbS12 r = kAbMk[which & 1](x); return (u64)r.a + (u64)r.b * 5u + r.c; }

NOINLINE static float ab_h_f0(float a, double b, u32 c, float d, float e) { return a + (float)b * (float)c - d * e; }
NOINLINE static float ab_h_f1(float a, double b, u32 c, float d, float e) { return a * d + e - (float)(b - (double)c); }
static const AbFF kAbFF[2] = { ab_h_f0, ab_h_f1 };
EXPORT float ab_fp_float(u32 which, float a, double b, u32 c, float d, float e) { return kAbFF[which & 1](a, b, c, d, e); }

/* ---- recursion, direct and mutual, with masked depth ---- */

EXPORT u64 ab_rec_tree(u32 depth, u64 seed) {
    depth &= 7;
    if (!depth) return seed;
    return (ab_rec_tree(depth - 1, seed * 0x9E3779B97F4A7C15ULL + depth) ^ ab_rec_tree(depth - 1, seed + 0x51u)) + depth;
}

/* Five arguments: the fifth (the depth) is on the stack. */
EXPORT u64 ab_rec_stack(u64 a, u64 b, u64 c, u64 d, u32 depth) {
    depth &= 5;
    if (!depth) return a + 2 * b + 3 * c + 4 * d;
    return ab_rec_stack(b, c, d, a ^ depth, depth - 1) - ab_rec_stack(c, d, a, b + 1, depth - 1);
}

EXPORT u32 ab_ping(u32 n, u32 acc);
NOINLINE static u32 ab_pong(u32 n, u32 acc) { n &= 15; return n == 0 ? acc : ab_ping(n - 1, acc * 3u + 1u); }
NOINLINE static u32 ab_pang(u32 n, u32 acc) { n &= 15; return n == 0 ? (acc ^ 0xA5u) : ab_pong(n - 1, acc ^ n); }
EXPORT u32 ab_ping(u32 n, u32 acc) { n &= 15; return n == 0 ? acc : ab_pang(n - 1, acc + n); }

/* Recursion that returns a struct through the hidden pointer on every level. */
EXPORT AbS16 ab_rec_struct(u32 n, u64 x) {
    n &= 3;
    AbS16 r = { x, x ^ 0xA5u };
    if (n) {
        AbS16 t = ab_rec_struct(n - 1, x * 3u + n);
        r.lo = t.lo + t.hi;
        r.hi = t.hi ^ x;
    }
    return r;
}

/* ---- tail calls ---- */

NOINLINE static u64 ab_h_tail6(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) { return (a ^ f) + b * c - (d | e); }
EXPORT u64 ab_tail6(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) { return ab_h_tail6(b, a, d, c, f, e); }

NOINLINE static AbS16 ab_h_mk16(u64 x) { AbS16 r = { x * 7u, x >> 3 }; return r; }
EXPORT AbS16 ab_tail_sret(u64 x, u64 y) { return ab_h_mk16(x ^ y); }

NOINLINE static u64 ab_h_t1(u64 x) { return x * 13u + 1u; }
NOINLINE static u64 ab_h_t2(u64 x) { return x ^ (x >> 9); }
EXPORT u64 ab_tail_branch(u64 x, u32 n) { if (n & 1) return ab_h_t1(x); return ab_h_t2(x + n); }

NOINLINE static float ab_h_tf(float a, double b) { return a * (float)b + 0.5f; }
EXPORT float ab_tail_float(float a, double b, u32 n) { return ab_h_tf(a * (float)n, b * 0.25); }

NOINLINE static u64 ab_h_s12f(AbS12 s, u64 k) { return ((u64)s.a + (u64)s.b * k) ^ s.c; }
EXPORT u64 ab_tail_struct(AbS12 s, u64 k) { return ab_h_s12f(s, k ^ 1u); }

/* ---- switches: dense 64-bit, bit test, sparse 64-bit, ranges, compare chain, float results ---- */

EXPORT u64 ab_sw64_dense(u64 k, u64 x) {
    switch (k & 15) {
    case 0: return x + 1;   case 1: return x * 3;   case 2: return x ^ 0x55u; case 3: return 0 - x;
    case 4: return x >> 2;  case 5: return x << 3;  case 6: return ~x;        case 7: return x - 9;
    default: return 0;
    }
}

EXPORT u32 ab_sw_bittest(u32 k) {
    switch (k & 63) {
    case 0: case 5: case 9: case 33: return 1;
    case 17: case 40: return 2;
    case 63: return 3;
    default: return 0;
    }
}

/* Half the inputs take a case key from a table, so random arguments reach every case. */
static const u64 kAbKeys[6] = { 0x0123456789ABCDEFULL, 0x1000000000000000ULL, 0x7FFFFFFFFFFFFFFFULL,
                                0xFFFFFFFF00000000ULL, 0x00000000DEADBEEFULL, 42 };
EXPORT u64 ab_sw64_sparse(u64 k, u64 x) {
    u64 key = (k & 1) ? kAbKeys[(k >> 1) % 6] : k;
    switch (key) {
    case 0x0123456789ABCDEFULL: return x + 1;
    case 0x1000000000000000ULL: return x * 2;
    case 0x7FFFFFFFFFFFFFFFULL: return x ^ 3;
    case 0xFFFFFFFF00000000ULL: return ~x;
    case 0x00000000DEADBEEFULL: return x >> 1;
    case 42: return x << 4;
    }
    return key & x;
}

EXPORT u32 ab_sw_range(u32 k) {
    switch (k & 255) {
    case 0 ... 9: return 1;
    case 10 ... 99: return 2;
    case 100 ... 199: return 3;
    default: return 4;
    }
}

static const u32 kAbChainKeys[4] = { 3, 77, 1000, 0xBEEF };
EXPORT i32 ab_sw_chain(u16 k) {
    u32 key = (k & 1) ? kAbChainKeys[(k >> 1) & 3] : k;
    switch (key) {
    case 3: return 30;
    case 77: return -7;
    case 1000: return 1;
    case 0xBEEF: return 99;
    default: return (i32)key - 50;
    }
}

EXPORT float ab_sw_float(u32 k) {
    switch (k & 3) {
    case 0: return 1.25f;
    case 1: return -0.5f;
    case 2: return 3.0e4f;
    default: return 0.0078125f;
    }
}

/* ---- computed goto through a table of label addresses ---- */

EXPORT u32 ab_goto_tab(u32 k, u32 acc) {
    static void *const kTab[4] = { &&c0, &&c1, &&c2, &&c3 };
    goto *kTab[k & 3];
c0:
    acc += 11u;
    goto out;
c1:
    acc = acc * 5u - 1u;
    goto out;
c2:
    acc ^= 0x3c3cu;
    goto out;
c3:
    acc = 0u - acc;
out:
    return acc;
}

EXPORT u64 ab_goto_loop(const u8 *code, u32 n, u64 x) {
    static void *const kDisp[4] = { &&op_add, &&op_xor, &&op_rot, &&op_end };
    u32 pc = 0, steps = 0;
    n &= 15;
    if (n == 0) return x;
    goto *kDisp[code[0] & 3];
op_add:
    x += code[pc];
    goto step;
op_xor:
    x ^= (u64)code[pc] << 8;
    goto step;
op_rot:
    x = (x << 7) | (x >> 57);
    goto step;
step:
    if (++steps >= 16 || ++pc >= n) goto op_end;
    goto *kDisp[code[pc] & 3];
op_end:
    return x;
}

/* ---- 128-bit integers: passed by pointer on this ABI, returned through a hidden pointer ---- */

EXPORT AbU128 ab_u128_add(AbU128 a, AbU128 b) { return a + b; }
EXPORT AbU128 ab_u128_mul(AbU128 a, AbU128 b) { return a * b; }
EXPORT AbU128 ab_u128_shl(AbU128 a, u32 n) { return a << (n & 127); }
EXPORT AbU128 ab_u128_shr(AbU128 a, u32 n) { return a >> (n & 127); }
EXPORT u32 ab_u128_cmp(AbU128 a, AbU128 b) { return (a < b) | ((a == b) << 1) | ((a > b) << 2); }
EXPORT AbU128 ab_u128_sub(AbU128 a, u64 b) { return a - b; }
EXPORT u64 ab_u128_addc(AbU128 a, AbU128 b, u64 *carry) { AbU128 r = a + b; *carry = r < a; return (u64)r ^ (u64)(r >> 64); }
EXPORT void ab_u128_accum(AbU128 *p, AbU128 v, u32 n) { n &= 7; for (u32 i = 0; i < n; i++) p[0] = p[0] * 3 + v; }
EXPORT double ab_u128_mix(u32 a, AbU128 b, double c, AbU128 d, u8 e) {
    return (double)(u64)(b >> 64) + c * (double)a + (double)(u64)d * (double)e;
}

/* ---- values live across calls: callee-saved GPRs and xmm6 to xmm15 ---- */

NOINLINE static u64 ab_h_bump(u64 x) { return x * 0x2545F4914F6CDD1DULL + 7u; }
EXPORT u64 ab_press_int(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h) {
    u64 v0 = a * 3u, v1 = b + 5u, v2 = c ^ 0x77u, v3 = d * d, v4 = e - 9u, v5 = f | 1u, v6 = g << 2, v7 = h ^ (g >> 1);
    u64 r = ab_h_bump(v0);
    r ^= v1 + ab_h_bump(v2);
    r += v3 * ab_h_bump(v4);
    r ^= ab_h_bump(v5) - v6;
    r += ab_h_bump(v7);
    return r + v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7;
}

NOINLINE static double ab_h_fb(double x) { return x * 1.0009765625 + 0.5; }
EXPORT double ab_press_fp(double a, double b, double c, double d, double e, double f, double g, double h) {
    double v0 = a * 2.0, v1 = b + 1.0, v2 = c - 0.25, v3 = d * 0.5, v4 = e + 3.0;
    double v5 = f * f, v6 = g - h, v7 = h * 1.5, v8 = a + b, v9 = c * d;
    double r = ab_h_fb(v0);
    r += v1 * ab_h_fb(v2);
    r += v3 * ab_h_fb(v4);
    r -= ab_h_fb(v5) * v6;
    r += ab_h_fb(v7) + v8 * ab_h_fb(v9);
    return r + v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9;
}

/* ---- 16-byte vectors: unaligned pointer traffic (movups), aligned locals (movaps) ---- */

EXPORT void ab_vscale4(float *dst, const float *src, float k) {
    __m128 v = _mm_loadu_ps(src);
    v = _mm_mul_ps(v, _mm_set1_ps(k));
    _mm_storeu_ps(dst, v);
}

EXPORT float ab_vsum_blocks(const float *src, u32 n) {
    __m128 acc = _mm_setzero_ps();
    n &= 7;
    for (u32 i = 0; i < n; i++) acc = _mm_add_ps(acc, _mm_loadu_ps(src + 4 * i));
    return _mm_cvtss_f32(_mm_mul_ps(acc, acc));
}

EXPORT u32 ab_vlocal(u32 seed) {
    _Alignas(16) float buf[4];
    __m128 t = _mm_set_ps((float)(seed & 7u), (float)((seed >> 3) & 7u), 2.5f, -1.0f);
    __m128 u = _mm_mul_ps(t, _mm_add_ps(t, t));
    _mm_store_ps(buf, u);
    return (u32)(i32)buf[0] + (u32)(i32)buf[3];
}

EXPORT void ab_copy32(void *dst, const void *src) { __builtin_memcpy(dst, src, 32); }
EXPORT void ab_s16_copy(AbS16 *dst, const AbS16 *src) { *dst = *src; }

/* ---- conditional selects, flag materialisation, address arithmetic ---- */

EXPORT u64 ab_sel_ne(u64 a, u64 b, u64 c, u64 d) { return a != b ? c : d; }
EXPORT u64 ab_sel_ne_mem(const u64 *p, u64 b, u64 d) { return p[0] != b ? p[1] : d ^ p[0]; }
EXPORT u32 ab_eq_reg(u64 a, u64 b) { return a == b; }
EXPORT u32 ab_eq_mem(const u32 *p, u32 k) { return p[1] == k; }
EXPORT u32 ab_flags_pack(u32 a, u32 b, u32 c) { return (a == 0) | ((b == 7) << 1) | ((c != 0) << 2); }
EXPORT u64 ab_lea_mix(u64 a, u64 b) { return a * 3 + b * 5 + 11; }
EXPORT u64 ab_lea_idx(const u32 *p, u32 i) { return p[i & 7] * 9u + p[(i >> 3) & 7]; }
