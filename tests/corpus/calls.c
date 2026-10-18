/* Calls: recursion, many arguments, function pointers, tail calls, variadics, big frames. */
#include "corpus.h"

EXPORT u32 fib(u32 n) { n &= 15; return n < 2 ? n : fib(n - 1) + fib(n - 2); }
EXPORT u32 ackermann(u32 m, u32 n) { m &= 1; n &= 7; if (!m) return n + 1; if (!n) return ackermann(m - 1, 1); return ackermann(m - 1, ackermann(m, n - 1)); }
NOINLINE static i64 many(i64 a, i64 b, i64 c, i64 d, i64 e, i64 f, i64 g, i64 h) { return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h; }
EXPORT i64 call_many(i64 s) { return many(s, s + 1, s + 2, s + 3, s + 4, s + 5, s + 6, s + 7) - many(8, 7, 6, 5, 4, 3, 2, s); }
typedef i32 (*binop)(i32, i32);
static i32 op_add(i32 a, i32 b) { return a + b; }
static i32 op_sub(i32 a, i32 b) { return a - b; }
static i32 op_mul(i32 a, i32 b) { return a * b; }
static i32 op_xor(i32 a, i32 b) { return a ^ b; }
static const binop kOps[4] = { op_add, op_sub, op_mul, op_xor };
EXPORT i32 dispatch(u32 which, i32 a, i32 b) { return kOps[which & 3](a, b); }
EXPORT i32 fold(const i32 *v, u32 n, u32 which) { binop f = kOps[which & 3]; i32 acc = 1; n &= 15; for (u32 i = 0; i < n; i++) acc = f(acc, v[i]); return acc; }
NOINLINE static i32 tail_target(i32 a, i32 b) { return a * 3 + b; }
EXPORT i32 tail_caller(i32 a, i32 b) { if (a & 1) return tail_target(b, a); return tail_target(a + 1, b - 1); }
EXPORT i64 sum_va(u32 n, ...) {
    __builtin_ms_va_list ap; __builtin_ms_va_start(ap, n); i64 s = 0; n &= 7;
    for (u32 i = 0; i < n; i++) s += __builtin_va_arg(ap, i64);
    __builtin_ms_va_end(ap); return s;
}
EXPORT i64 call_va(i64 a) { return sum_va(6, a, a * 2, (i64)3, a - 1, (i64)-5, a ^ 0x55); }
EXPORT u32 big_frame(u32 seed) {
    u32 buf[512]; for (u32 i = 0; i < 512; i++) { seed = seed * 1103515245u + 12345u; buf[i] = seed; }
    u32 x = 0; for (u32 i = 0; i < 512; i += 7) x ^= buf[i] + buf[511 - i]; return x;
}
EXPORT i32 mutual_even(u32 n);
NOINLINE static i32 mutual_odd(u32 n) { return n == 0 ? 0 : mutual_even(n - 1); }
EXPORT i32 mutual_even(u32 n) { n &= 31; return n == 0 ? 1 : mutual_odd(n - 1); }
EXPORT i64 callback_walk(const i32 *v, u32 n, i64 (*cb)(i32, void *), void *user) { i64 s = 0; n &= 7; for (u32 i = 0; i < n; i++) s += cb(v[i], user); return s; }
