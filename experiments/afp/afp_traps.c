// afp_traps.c - small demonstrations of the traps listed in docs/findings/11-float-mode.md.
// Each block prints what happened; nothing here is a pass/fail gate.   Build: clang -O2 afp_traps.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <arm_neon.h>

static uint64_t rd_fpcr(void) { return __builtin_arm_rsr64("fpcr"); }
static void wr_fpcr(uint64_t v) { __builtin_arm_wsr64("fpcr", v); }
static uint32_t u32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
#define NOINL __attribute__((noinline))

// ---- 1. operand order: x86 addss d,s returns d's NaN when both are NaN; fadd returns its first operand's.
NOINL float add_load_then_arg(const float *p, float x) { return *p + x; }     // x86 order: *p first
NOINL float add_arg_then_load(float x, const float *p) { return x + *p; }     // x86 order: x first
NOINL void  add_twice(const float *restrict a, const float *restrict b, float *restrict r1, float *restrict r2) { *r1 = *a + *b; *r2 = *b + *a; }   // CSE merges: b+a == a+b
NOINL float mul_const_second(float x) { return 2.0f * x; }

// ---- 2. contraction
NOINL float muladd_expr(float a, float b, float c) { return a * b + c; }                 // clang default -ffp-contract=on: fmadd
NOINL float muladd_stmt(float a, float b, float c) { volatile float t = a * b; return t + c; }

// ---- 3. constant folding of invalid operations (compile time uses a +NaN, hardware under AH makes -NaN)
NOINL float const_inf_minus_inf(void) { volatile float z = 1.0f / 0.0f; (void)z; return INFINITY - INFINITY; }
NOINL float const_zero_div(float z) { return 0.0f / z; }       // folded only if z known
NOINL float call_const_zero_div(void) { return const_zero_div(0.0f); }

// ---- 5. FNEG/FABS leave NaN bit patterns alone under AH; clang builds the 0x7fff..ffff mask as fneg(all-ones)
typedef union { uint8_t b[16]; uint64_t q[2]; double pd[2]; } Xmm;
NOINL void andpd_imm_inplace(Xmm *d) { d->q[0] &= 0x7fffffffffffffffull; d->q[1] &= 0x7fffffffffffffffull; }   // andpd xmm, [fabs mask] with a known mask
NOINL void andpd_imm_inplace_gpr(Xmm *d) __attribute__((optnone));
void andpd_imm_inplace_gpr(Xmm *d) { d->q[0] &= 0x7fffffffffffffffull; d->q[1] &= 0x7fffffffffffffffull; }
NOINL double cbrt_call(double x) { double (*volatile p)(double) = cbrt; return p(x); }

// ---- 4. where FPCR does and does not follow you
static volatile uint64_t thread_fpcr, handler_fpcr;
static void *thr(void *arg) { (void)arg; thread_fpcr = rd_fpcr(); return 0; }
static void on_sig(int s) { (void)s; handler_fpcr = rd_fpcr(); }

int main(void) {
    uint64_t base = rd_fpcr();
    float n1 = f32(0x7fc00001), n2 = f32(0x7fc00002);
    printf("== 1. operand order, both operands NaN (a=7fc00001, b=7fc00002); x86 addss returns the destination (first) = 7fc00001\n");
    for (int m = 0; m < 2; m++) {
        wr_fpcr(base | (m ? 2 : 0));
        float r1, r2;
        printf("  FPCR.AH=%d: *p + x -> %08x | x + *p (x86 would want x first) -> %08x | ", m, u32(add_load_then_arg(&n1, n2)), u32(add_arg_then_load(n1, &n2)));
        add_twice(&n1, &n2, &r1, &r2);
        printf("a+b, b+a stored separately -> %08x %08x (x86: 7fc00001 7fc00002)\n", u32(r1), u32(r2));
        wr_fpcr(base);
    }
    printf("== 2. contraction: a*b+c in one C expression becomes fmadd (one rounding); x86 mulss then addss rounds twice\n");
    {
        uint32_t s = 12345; int shown = 0, diff = 0, N = 100000;
        for (int i = 0; i < N; i++) {
            s = s * 1664525u + 1013904223u; float a = 1.0f + (float)(s >> 9) / 8388608.0f;
            s = s * 1664525u + 1013904223u; float b = 1.0f + (float)(s >> 9) / 8388608.0f;
            s = s * 1664525u + 1013904223u; float c = -2.0f * (1.0f + (float)(s >> 9) / 8388608.0f);
            uint32_t x = u32(muladd_expr(a, b, c)), y = u32(muladd_stmt(a, b, c));
            if (x != y) { diff++; if (!shown++) printf("  e.g. a=%08x b=%08x c=%08x: single expression %08x, separate statements %08x\n", u32(a), u32(b), u32(c), x, y); }
        }
        printf("  %d of %d random triples differ\n", diff, N);
    }
    printf("== 3. constant folding of invalid operations (x86 / AH hardware: ffc00000)\n");
    wr_fpcr(base | 2);
    volatile float zero = 0.0f;
    printf("  runtime 0/0 under AH: %08x ; INF-INF folded by the compiler: %08x ; 0/0 folded after inlining: %08x\n", u32(zero / zero), u32(const_inf_minus_inf()), u32(call_const_zero_div()));
    wr_fpcr(base);
    printf("== 5. bit masks built with fneg: lifted-style andpd on a known mask (fabs of both lanes), and libm\n");
    for (int m = 0; m < 2; m++) {
        wr_fpcr(base | (m ? 2 : 0));
        Xmm x = {.pd = {-1.5, -2.5}}, y = x;
        andpd_imm_inplace(&x); andpd_imm_inplace_gpr(&y);
        printf("  AH=%d: clang -O2 (NEON mask)  lanes %g %g | optnone (GPR and) lanes %g %g | cbrt(-8.0) = %g (libm)\n", m, x.pd[0], x.pd[1], y.pd[0], y.pd[1], cbrt_call(-8.0));
        wr_fpcr(base);
    }
    printf("== 6. FPCR inheritance (parent thread has AH set)\n");
    wr_fpcr(base | 2);
    pthread_t t; pthread_create(&t, 0, thr, 0); pthread_join(t, 0);
    signal(SIGUSR1, on_sig); raise(SIGUSR1);
    printf("  new pthread starts with FPCR=%#llx ; signal handler runs with FPCR=%#llx ; parent FPCR=%#llx\n", (unsigned long long)thread_fpcr, (unsigned long long)handler_fpcr, (unsigned long long)rd_fpcr());
    wr_fpcr(base);
    return 0;
}
