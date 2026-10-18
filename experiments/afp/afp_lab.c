// afp_lab.c - arm64 side of the SSE-on-ARM float experiment (issue #11).
//
// Every x86 SSE operation under test is written here as ordinary C on a union register,
// the way the lifter would emit it, in one or more candidate formulations ("variants").
// The driver (afp_lab.py) feeds records on stdin, this program sets FPCR, runs one variant over
// every record, restores FPCR, and writes results to stdout.  No disk, no libc float code runs
// while FPCR is non-default except what is inside the variant itself.
//
//   afp_lab list                       print "name kind" for every variant
//   afp_lab run <variant> <fpcr-hex>   stdin: N x {a[16], b[16]}   stdout: N x {xmm0[16], gpr[8]}
//
// Record semantics mirror the x86 harness code:  xmm0 = a, xmm1 = b, rax = a.q[0]
// (kind 1 "g2x": xmm0 = b, the integer source is rax).  Results: xmm0 and rax after the op.
// For flag-setting compares rax = the six status flags of RFLAGS (mask 0x8d5) after the op.
//
// Build: clang -O2 -ffp-contract=off afp_lab.c afp_lab_errno.c -o out/afp_lab
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <arm_neon.h>

typedef union { uint8_t b[16]; uint32_t d[4]; uint64_t q[2]; float f[4]; double pd[2];
                int32_t i[4]; int64_t l[2]; } Xmm;
typedef void (*opfn)(Xmm *d, const Xmm *s, uint64_t *g);

enum { K_XX = 0, K_G2X = 1 };
typedef struct { const char *name; opfn fn; int kind; } Variant;

#define F32(x)  ((x))
// ---------------------------------------------------------------------------------------------
// arithmetic: add sub mul div, scalar single/double and packed single/double
// ---------------------------------------------------------------------------------------------
#define ARITH(NAME, OP) \
static void NAME##ss__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0]  = d->f[0]  OP s->f[0]; } \
static void NAME##sd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->pd[0] = d->pd[0] OP s->pd[0]; } \
static void NAME##ps__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t = *d, u = *s; \
    for (int i = 0; i < 4; i++) t.f[i] = t.f[i] OP u.f[i]; *d = t; } \
static void NAME##pd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t = *d, u = *s; \
    for (int i = 0; i < 2; i++) t.pd[i] = t.pd[i] OP u.pd[i]; *d = t; }
ARITH(add, +) ARITH(sub, -) ARITH(mul, *) ARITH(div, /)

// operand-order-locked scalar versions (inline asm) to expose compiler commuting of fadd/fmul
#define ASMOP(NAME, INSN) \
static void NAME##ss__asm(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float r; \
    __asm__(INSN " %s0, %s1, %s2" : "=w"(r) : "w"(d->f[0]), "w"(s->f[0])); d->f[0] = r; } \
static void NAME##sd__asm(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; double r; \
    __asm__(INSN " %d0, %d1, %d2" : "=w"(r) : "w"(d->pd[0]), "w"(s->pd[0])); d->pd[0] = r; }
ASMOP(add, "fadd") ASMOP(mul, "fmul") ASMOP(sub, "fsub") ASMOP(div, "fdiv")

// ---------------------------------------------------------------------------------------------
// sqrt
// ---------------------------------------------------------------------------------------------
static void sqrtss__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0]  = sqrtf(s->f[0]); }
static void sqrtsd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->pd[0] = sqrt(s->pd[0]); }
static void sqrtps__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t, u = *s;
    for (int i = 0; i < 4; i++) t.f[i] = sqrtf(u.f[i]); *d = t; }
static void sqrtpd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t, u = *s;
    for (int i = 0; i < 2; i++) t.pd[i] = sqrt(u.pd[i]); *d = t; }
// real call into libm (volatile pointer defeats inlining)
static float  (*volatile p_sqrtf)(float)   = sqrtf;
static double (*volatile p_sqrt)(double)   = sqrt;
static void sqrtss__libm(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0]  = p_sqrtf(s->f[0]); }
static void sqrtsd__libm(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->pd[0] = p_sqrt(s->pd[0]); }
// compiled with -fmath-errno in afp_lab_errno.c
void sqrtss__errno(Xmm *d, const Xmm *s, uint64_t *g);
void sqrtsd__errno(Xmm *d, const Xmm *s, uint64_t *g);

// ---------------------------------------------------------------------------------------------
// min / max.  x86: minss d,s = (d < s) ? d : s   (so s is returned for NaN and for equal incl. +-0)
//               maxss d,s = (d > s) ? d : s
// ---------------------------------------------------------------------------------------------
#define MINMAX(NAME, CMP, INSN, VINSNF, VINSND, LIBF, LIBD) \
static void NAME##ss__ternary(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; \
    float a = d->f[0], b = s->f[0]; d->f[0] = (a CMP b) ? a : b; } \
static void NAME##sd__ternary(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; \
    double a = d->pd[0], b = s->pd[0]; d->pd[0] = (a CMP b) ? a : b; } \
static void NAME##ps__ternary(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t = *d, u = *s; \
    for (int i = 0; i < 4; i++) t.f[i] = (t.f[i] CMP u.f[i]) ? t.f[i] : u.f[i]; *d = t; } \
static void NAME##pd__ternary(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t = *d, u = *s; \
    for (int i = 0; i < 2; i++) t.pd[i] = (t.pd[i] CMP u.pd[i]) ? t.pd[i] : u.pd[i]; *d = t; } \
static void NAME##ss__fmin(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float r; \
    __asm__(INSN " %s0, %s1, %s2" : "=w"(r) : "w"(d->f[0]), "w"(s->f[0])); d->f[0] = r; } \
static void NAME##sd__fmin(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; double r; \
    __asm__(INSN " %d0, %d1, %d2" : "=w"(r) : "w"(d->pd[0]), "w"(s->pd[0])); d->pd[0] = r; } \
static void NAME##ps__fmin(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; \
    float32x4_t a = vld1q_f32(d->f), b = vld1q_f32(s->f); vst1q_f32(d->f, VINSNF(a, b)); } \
static void NAME##pd__fmin(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; \
    float64x2_t a = vld1q_f64(d->pd), b = vld1q_f64(s->pd); vst1q_f64(d->pd, VINSND(a, b)); } \
static void NAME##ss__libm(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0]  = LIBF(d->f[0], s->f[0]); } \
static void NAME##sd__libm(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->pd[0] = LIBD(d->pd[0], s->pd[0]); }
MINMAX(min, <, "fmin", vminq_f32, vminq_f64, fminf, fmin)
MINMAX(max, >, "fmax", vmaxq_f32, vmaxq_f64, fmaxf, fmax)

// ---------------------------------------------------------------------------------------------
// comiss / ucomiss / comisd / ucomisd.  Result: ZF PF CF (bits 6 2 0) of RFLAGS, OF SF AF = 0.
//   unordered: ZF=PF=CF=1;  a>b: 0 0 0;  a<b: CF=1;  a==b: ZF=1
// ---------------------------------------------------------------------------------------------
#define FLAGS_C(T, A, B) \
    uint64_t un = ((A) != (A)) | ((B) != (B)); \
    uint64_t zf = un | ((A) == (B)), pf = un, cf = un | ((A) < (B)); \
    *g = (zf << 6) | (pf << 2) | cf;
static void comiss__c(Xmm *d, const Xmm *s, uint64_t *g) { float a = d->f[0], b = s->f[0]; FLAGS_C(float, a, b) }
static void comisd__c(Xmm *d, const Xmm *s, uint64_t *g) { double a = d->pd[0], b = s->pd[0]; FLAGS_C(double, a, b) }
static void ucomiss__c(Xmm *d, const Xmm *s, uint64_t *g) { float a = d->f[0], b = s->f[0]; FLAGS_C(float, a, b) }
static void ucomisd__c(Xmm *d, const Xmm *s, uint64_t *g) { double a = d->pd[0], b = s->pd[0]; FLAGS_C(double, a, b) }
// same thing from the native fcmp flags: equal Z,C; less N; greater C; unordered C,V
static uint64_t nzcv_to_x86(uint64_t nzcv) {
    uint64_t n = (nzcv >> 31) & 1, z = (nzcv >> 30) & 1, v = (nzcv >> 28) & 1;
    return ((z | v) << 6) | (v << 2) | (n | v);
}
static void comiss__nzcv(Xmm *d, const Xmm *s, uint64_t *g) { uint64_t f;
    __asm__ volatile("fcmp %s1, %s2\n\tmrs %0, nzcv" : "=r"(f) : "w"(d->f[0]), "w"(s->f[0]) : "cc"); *g = nzcv_to_x86(f); }
static void comisd__nzcv(Xmm *d, const Xmm *s, uint64_t *g) { uint64_t f;
    __asm__ volatile("fcmp %d1, %d2\n\tmrs %0, nzcv" : "=r"(f) : "w"(d->pd[0]), "w"(s->pd[0]) : "cc"); *g = nzcv_to_x86(f); }

// ---------------------------------------------------------------------------------------------
// cmpss/cmpps/cmpsd/cmppd with predicates 0..7
// ---------------------------------------------------------------------------------------------
#define P0(a,b) ((a) == (b))
#define P1(a,b) ((a) <  (b))
#define P2(a,b) ((a) <= (b))
#define P3(a,b) (((a) != (a)) | ((b) != (b)))
#define P4(a,b) (!((a) == (b)))
#define P5(a,b) (!((a) <  (b)))
#define P6(a,b) (!((a) <= (b)))
#define P7(a,b) (((a) == (a)) & ((b) == (b)))
#define CMPFAM(N, P) \
static void cmp##N##ss__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float a = d->f[0], b = s->f[0]; \
    d->d[0] = P(a, b) ? 0xffffffffu : 0u; } \
static void cmp##N##sd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; double a = d->pd[0], b = s->pd[0]; \
    d->q[0] = P(a, b) ? ~0ull : 0ull; } \
static void cmp##N##ps__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t = *d, u = *s; \
    for (int i = 0; i < 4; i++) t.d[i] = P(t.f[i], u.f[i]) ? 0xffffffffu : 0u; *d = t; } \
static void cmp##N##pd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm t = *d, u = *s; \
    for (int i = 0; i < 2; i++) t.q[i] = P(t.pd[i], u.pd[i]) ? ~0ull : 0ull; *d = t; }
CMPFAM(0, P0) CMPFAM(1, P1) CMPFAM(2, P2) CMPFAM(3, P3) CMPFAM(4, P4) CMPFAM(5, P5) CMPFAM(6, P6) CMPFAM(7, P7)

// ---------------------------------------------------------------------------------------------
// conversions int -> float (rounding follows FPCR.RMode, as x86 follows MXCSR.RC)
// ---------------------------------------------------------------------------------------------
static void cvtsi2ss32__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)s; d->f[0]  = (float)(int32_t)*g; }
static void cvtsi2ss64__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)s; d->f[0]  = (float)(int64_t)*g; }
static void cvtsi2sd32__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)s; d->pd[0] = (double)(int32_t)*g; }
static void cvtsi2sd64__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)s; d->pd[0] = (double)(int64_t)*g; }

// ---------------------------------------------------------------------------------------------
// conversions float -> int.  x86 returns the "integer indefinite" (INT_MIN) for NaN and any
// result that does not fit.  ARM fcvtzs saturates and maps NaN to 0.
//   naive:   (int32_t)v                         (UB in C when out of range; compiles to fcvtzs)
//   checked: range test first, INT_MIN otherwise
// "cvtt" truncates; "cvt" rounds in the current mode (frinti) and then range-checks the *rounded* value.
// ---------------------------------------------------------------------------------------------
#define LIM32F 2147483648.0f
#define LIM32  2147483648.0
#define LIM64F 9223372036854775808.0f
#define LIM64  9223372036854775808.0
static void cvttss2si32__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint32_t)(int32_t)s->f[0]; }
static void cvttss2si64__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint64_t)(int64_t)s->f[0]; }
static void cvttsd2si32__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint32_t)(int32_t)s->pd[0]; }
static void cvttsd2si64__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint64_t)(int64_t)s->pd[0]; }
static void cvttss2si32__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; float v = s->f[0];
    *g = (uint32_t)((v >= -LIM32F && v < LIM32F) ? (int32_t)v : INT32_MIN); }
static void cvttss2si64__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; float v = s->f[0];
    *g = (uint64_t)((v >= -LIM64F && v < LIM64F) ? (int64_t)v : INT64_MIN); }
static void cvttsd2si32__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; double v = s->pd[0];
    *g = (uint32_t)((v > -2147483649.0 && v < LIM32) ? (int32_t)v : INT32_MIN); }
static void cvttsd2si64__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; double v = s->pd[0];
    *g = (uint64_t)((v >= -LIM64 && v < LIM64) ? (int64_t)v : INT64_MIN); }
static void cvtss2si32__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint32_t)(int32_t)rintf(s->f[0]); }
static void cvtss2si64__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint64_t)(int64_t)rintf(s->f[0]); }
static void cvtsd2si32__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint32_t)(int32_t)rint(s->pd[0]); }
static void cvtsd2si64__naive(Xmm *d, const Xmm *s, uint64_t *g)   { (void)d; *g = (uint64_t)(int64_t)rint(s->pd[0]); }
static void cvtss2si32__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; float t = rintf(s->f[0]);
    *g = (uint32_t)((t >= -LIM32F && t < LIM32F) ? (int32_t)t : INT32_MIN); }
static void cvtss2si64__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; float t = rintf(s->f[0]);
    *g = (uint64_t)((t >= -LIM64F && t < LIM64F) ? (int64_t)t : INT64_MIN); }
static void cvtsd2si32__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; double t = rint(s->pd[0]);
    *g = (uint32_t)((t >= -LIM32 && t < LIM32) ? (int32_t)t : INT32_MIN); }
static void cvtsd2si64__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; double t = rint(s->pd[0]);
    *g = (uint64_t)((t >= -LIM64 && t < LIM64) ? (int64_t)t : INT64_MIN); }
// round with a fixed-mode native instruction instead of the current mode (shows why frinti is needed)
static void cvtss2si32__lrint_nearest(Xmm *d, const Xmm *s, uint64_t *g) { (void)d; float v = s->f[0];
    int32_t r; __asm__("fcvtns %w0, %s1" : "=r"(r) : "w"(v)); *g = (uint32_t)r; }

// ---------------------------------------------------------------------------------------------
// float <-> float and packed int <-> float conversions
// ---------------------------------------------------------------------------------------------
static void cvtss2sd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->pd[0] = (double)s->f[0]; }
static void cvtsd2ss__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0]  = (float)s->pd[0]; }
static void cvtps2pd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float a = s->f[0], b = s->f[1];
    d->pd[0] = (double)a; d->pd[1] = (double)b; }
static void cvtpd2ps__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float a = (float)s->pd[0], b = (float)s->pd[1];
    d->f[0] = a; d->f[1] = b; d->d[2] = 0; d->d[3] = 0; }
static void cvtdq2ps__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.f[i] = (float)u.i[i]; *d = t; }
static void cvtdq2pd__c(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; int32_t a = s->i[0], b = s->i[1];
    d->pd[0] = (double)a; d->pd[1] = (double)b; }
static int32_t cvt_f32_i32_chk(float v) { float t = rintf(v);  return (t >= -LIM32F && t < LIM32F) ? (int32_t)t : INT32_MIN; }
static int32_t cvtt_f32_i32_chk(float v) {                     return (v >= -LIM32F && v < LIM32F) ? (int32_t)v : INT32_MIN; }
static int32_t cvt_f64_i32_chk(double v) { double t = rint(v); return (t >= -LIM32 && t < LIM32) ? (int32_t)t : INT32_MIN; }
static int32_t cvtt_f64_i32_chk(double v) {                    return (v > -2147483649.0 && v < LIM32) ? (int32_t)v : INT32_MIN; }
static void cvtps2dq__naive(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.i[i] = (int32_t)rintf(u.f[i]); *d = t; }
static void cvttps2dq__naive(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.i[i] = (int32_t)u.f[i]; *d = t; }
static void cvtps2dq__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.i[i] = cvt_f32_i32_chk(u.f[i]); *d = t; }
static void cvttps2dq__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.i[i] = cvtt_f32_i32_chk(u.f[i]); *d = t; }
static void cvtpd2dq__naive(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; int32_t a = (int32_t)rint(s->pd[0]), b = (int32_t)rint(s->pd[1]);
    d->i[0] = a; d->i[1] = b; d->q[1] = 0; }
static void cvttpd2dq__naive(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; int32_t a = (int32_t)s->pd[0], b = (int32_t)s->pd[1];
    d->i[0] = a; d->i[1] = b; d->q[1] = 0; }
static void cvtpd2dq__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; int32_t a = cvt_f64_i32_chk(s->pd[0]), b = cvt_f64_i32_chk(s->pd[1]);
    d->i[0] = a; d->i[1] = b; d->q[1] = 0; }
static void cvttpd2dq__checked(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; int32_t a = cvtt_f64_i32_chk(s->pd[0]), b = cvtt_f64_i32_chk(s->pd[1]);
    d->i[0] = a; d->i[1] = b; d->q[1] = 0; }

// ---------------------------------------------------------------------------------------------
// reciprocal / reciprocal square root approximations.  x86 is a table (hardware); the emulator
// computes an exact quotient.  Candidates: exact divide, native estimate, estimate + Newton step.
// ---------------------------------------------------------------------------------------------
static void rcpss__div(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0] = 1.0f / s->f[0]; }
static void rcpps__div(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.f[i] = 1.0f / u.f[i]; *d = t; }
static void rsqrtss__div(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0] = 1.0f / sqrtf(s->f[0]); }
static void rsqrtps__div(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.f[i] = 1.0f / sqrtf(u.f[i]); *d = t; }
static float frecpe_s(float x)  { float r; __asm__("frecpe %s0, %s1" : "=w"(r) : "w"(x)); return r; }
static float frecps_s(float a, float b) { float r; __asm__("frecps %s0, %s1, %s2" : "=w"(r) : "w"(a), "w"(b)); return r; }
static float frsqrte_s(float x) { float r; __asm__("frsqrte %s0, %s1" : "=w"(r) : "w"(x)); return r; }
static float frsqrts_s(float a, float b) { float r; __asm__("frsqrts %s0, %s1, %s2" : "=w"(r) : "w"(a), "w"(b)); return r; }
static void rcpss__frecpe(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0] = frecpe_s(s->f[0]); }
static void rcpps__frecpe(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.f[i] = frecpe_s(u.f[i]); *d = t; }
static void rcpss__frecpe_nr(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float x = s->f[0], e = frecpe_s(x);
    d->f[0] = e * frecps_s(x, e); }
static void rsqrtss__frsqrte(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0] = frsqrte_s(s->f[0]); }
static void rsqrtps__frsqrte(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; Xmm u = *s, t;
    for (int i = 0; i < 4; i++) t.f[i] = frsqrte_s(u.f[i]); *d = t; }
static void rsqrtss__frsqrte_nr(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; float x = s->f[0], e = frsqrte_s(x);
    d->f[0] = e * frsqrts_s(x * e, e); }

// ---------------------------------------------------------------------------------------------
#define V(n)      { #n, n, K_XX }
#define VG(n)     { #n, n, K_G2X }
static const Variant variants[] = {
#define ARITH_V(N) V(N##ss__c), V(N##sd__c), V(N##ps__c), V(N##pd__c), V(N##ss__asm), V(N##sd__asm)
    ARITH_V(add), ARITH_V(sub), ARITH_V(mul), ARITH_V(div),
    V(sqrtss__c), V(sqrtsd__c), V(sqrtps__c), V(sqrtpd__c), V(sqrtss__libm), V(sqrtsd__libm),
    V(sqrtss__errno), V(sqrtsd__errno),
#define MM_V(N) V(N##ss__ternary), V(N##sd__ternary), V(N##ps__ternary), V(N##pd__ternary), \
                V(N##ss__fmin), V(N##sd__fmin), V(N##ps__fmin), V(N##pd__fmin), V(N##ss__libm), V(N##sd__libm)
    MM_V(min), MM_V(max),
    V(comiss__c), V(comisd__c), V(ucomiss__c), V(ucomisd__c), V(comiss__nzcv), V(comisd__nzcv),
#define CMP_V(N) V(cmp##N##ss__c), V(cmp##N##sd__c), V(cmp##N##ps__c), V(cmp##N##pd__c)
    CMP_V(0), CMP_V(1), CMP_V(2), CMP_V(3), CMP_V(4), CMP_V(5), CMP_V(6), CMP_V(7),
    VG(cvtsi2ss32__c), VG(cvtsi2ss64__c), VG(cvtsi2sd32__c), VG(cvtsi2sd64__c),
    V(cvttss2si32__naive), V(cvttss2si64__naive), V(cvttsd2si32__naive), V(cvttsd2si64__naive),
    V(cvttss2si32__checked), V(cvttss2si64__checked), V(cvttsd2si32__checked), V(cvttsd2si64__checked),
    V(cvtss2si32__naive), V(cvtss2si64__naive), V(cvtsd2si32__naive), V(cvtsd2si64__naive),
    V(cvtss2si32__checked), V(cvtss2si64__checked), V(cvtsd2si32__checked), V(cvtsd2si64__checked),
    V(cvtss2si32__lrint_nearest),
    V(cvtss2sd__c), V(cvtsd2ss__c), V(cvtps2pd__c), V(cvtpd2ps__c), V(cvtdq2ps__c), V(cvtdq2pd__c),
    V(cvtps2dq__naive), V(cvttps2dq__naive), V(cvtps2dq__checked), V(cvttps2dq__checked),
    V(cvtpd2dq__naive), V(cvttpd2dq__naive), V(cvtpd2dq__checked), V(cvttpd2dq__checked),
    V(rcpss__div), V(rcpps__div), V(rsqrtss__div), V(rsqrtps__div),
    V(rcpss__frecpe), V(rcpps__frecpe), V(rcpss__frecpe_nr),
    V(rsqrtss__frsqrte), V(rsqrtps__frsqrte), V(rsqrtss__frsqrte_nr),
};
#define NVAR (sizeof variants / sizeof variants[0])

static uint64_t rd_fpcr(void) { return __builtin_arm_rsr64("fpcr"); }
static void wr_fpcr(uint64_t v) { __builtin_arm_wsr64("fpcr", v); }

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "list")) {
        for (size_t i = 0; i < NVAR; i++) printf("%s %d\n", variants[i].name, variants[i].kind);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "fpcr")) { printf("%llx\n", (unsigned long long)rd_fpcr()); return 0; }
    if (argc != 4 || strcmp(argv[1], "run")) { fprintf(stderr, "usage: afp_lab list | run <variant> <fpcr-hex>\n"); return 2; }
    const Variant *v = NULL;
    for (size_t i = 0; i < NVAR; i++) if (!strcmp(variants[i].name, argv[2])) v = &variants[i];
    if (!v) { fprintf(stderr, "unknown variant %s\n", argv[2]); return 2; }
    uint64_t want = strtoull(argv[3], NULL, 16);

    size_t cap = 1 << 20, len = 0; uint8_t *in = malloc(cap);
    for (;;) { if (len == cap) { cap *= 2; in = realloc(in, cap); }
               size_t n = fread(in + len, 1, cap - len, stdin); if (!n) break; len += n; }
    size_t N = len / 32;
    uint8_t *out = malloc(N * 24 + 1);
    opfn fn = v->fn; int kind = v->kind;

    uint64_t saved = rd_fpcr();
    wr_fpcr(want);
    uint64_t got = rd_fpcr();
    for (size_t i = 0; i < N; i++) {
        Xmm x0, x1; uint64_t g;
        memcpy(&x0, in + i * 32, 16); memcpy(&x1, in + i * 32 + 16, 16);
        memcpy(&g, in + i * 32, 8);
        if (kind == K_G2X) x0 = x1;
        fn(&x0, &x1, &g);
        memcpy(out + i * 24, &x0, 16); memcpy(out + i * 24 + 16, &g, 8);
    }
    wr_fpcr(saved);
    if ((got & 0x7) != (want & 0x7) || (got >> 22 & 7) != (want >> 22 & 7)) { fprintf(stderr, "FPCR readback %llx != %llx\n", (unsigned long long)got, (unsigned long long)want); return 3; }
    fwrite(out, 24, N, stdout);
    return 0;
}
