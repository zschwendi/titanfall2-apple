/* Integer and pointer arithmetic at every width. */
#include "corpus.h"

EXPORT u32 mix32(u32 a, u32 b) { a ^= b >> 3; a *= 0x9E3779B1u; a = (a << 13) | (a >> 19); return a + (b ^ 0xDEADBEEFu); }
EXPORT u64 mix64(u64 a, u64 b) { a ^= a >> 33; a *= 0xff51afd7ed558ccdULL; a ^= b; a = (a << 31) | (a >> 33); return a - (b >> 7); }
EXPORT i32 sdivmod(i32 a, i32 b, i32 *rem) { if (b == 0 || (a == (i32)0x80000000 && b == -1)) { *rem = 0; return 0; } *rem = a % b; return a / b; }
EXPORT u64 udivmod64(u64 a, u64 b, u64 *rem) { if (!b) { *rem = a; return 0; } *rem = a % b; return a / b; }
EXPORT i64 sdiv64(i64 a, i64 b) { if (b == 0 || (a == (i64)0x8000000000000000LL && b == -1)) return 0; return a / b + a % b; }
EXPORT u64 mulhi(u64 a, u64 b) { return (u64)(((unsigned __int128)a * b) >> 64); }
EXPORT i64 smulhi(i64 a, i64 b) { return (i64)(((__int128)a * b) >> 64); }
EXPORT u8 narrow8(u8 a, u8 b, i8 c) { u8 r = (u8)(a + b); r ^= (u8)(c >> 1); r = (u8)((r << 3) | (r >> 5)); return (u8)(r - (u8)c); }
EXPORT u16 narrow16(u16 a, i16 b) { u16 r = (u16)(a * 31u + (u16)b); r = (u16)((r >> 4) | (r << 12)); return (u16)(r ^ (u16)(b >> 2)); }
EXPORT i64 widen(i8 a, i16 b, i32 c, u8 d, u16 e, u32 f) { return (i64)a + (i64)b * 3 + (i64)c - (i64)d + (i64)e * 5 + (i64)f; }
EXPORT u32 shifts(u32 v, u32 n) { u32 s = n & 31; return (v << s) ^ (v >> s) ^ (u32)((i32)v >> s); }
EXPORT u64 shifts64(u64 v, u32 n) { u32 s = n & 63; return (v << s) + (v >> s) + (u64)((i64)v >> s); }
EXPORT u32 popcount32(u32 v) { u32 c = 0; while (v) { v &= v - 1; c++; } return c; }
EXPORT u32 clz32(u32 v) { u32 n = 0; if (!v) return 32; while (!(v & 0x80000000u)) { v <<= 1; n++; } return n; }
EXPORT u32 bswap32(u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }
EXPORT i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
EXPORT i32 sign(i64 v) { return (v > 0) - (v < 0); }
EXPORT u32 select(u32 a, u32 b, u32 c, u32 d) { return a < b ? (c >= d ? a + c : b - d) : (c != d ? a ^ d : b | c); }
EXPORT u64 carry_chain(u64 a0, u64 a1, u64 b0, u64 b1, u64 *hi) { u64 lo = a0 + b0; u64 c = lo < a0; *hi = a1 + b1 + c; return lo; }
EXPORT u64 borrow_chain(u64 a0, u64 a1, u64 b0, u64 b1, u64 *hi) { u64 lo = a0 - b0; u64 c = a0 < b0; *hi = a1 - b1 - c; return lo; }
EXPORT i64 ptr_diff(const i32 *a, const i32 *b) { return a - b; }
EXPORT u32 collatz(u32 n) { u32 steps = 0; while (n > 1 && steps < 1000) { n = (n & 1) ? 3 * n + 1 : n >> 1; steps++; } return steps; }
EXPORT u32 gcd(u32 a, u32 b) { while (b) { u32 t = a % b; a = b; b = t; } return a; }
EXPORT u32 isqrt(u32 n) { u32 r = 0, bit = 1u << 30; while (bit > n) bit >>= 2; while (bit) { if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; } return r; }
