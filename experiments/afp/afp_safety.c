// afp_safety.c - is it safe to leave FPCR.AH (and NEP) on for a whole thread?
//
// Runs libm, compiler-generated float code and string<->float conversion once per FPCR setting on
// the same inputs and compares result bits against FPCR=0.  Every difference is classified:
//   nan-input    an input was a NaN (undefined territory: NaN selection rules are what AH changes)
//   nan-result   no NaN input, but one of the two results is a NaN (domain error: sqrt(-1), inf-inf)
//   REAL         everything else: finite/inf inputs, both results not NaN  <- the ones that matter
// Built at several -O levels by afp_safety.py.
//
//   afp_safety            run, print per-function counts and the first REAL differences
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <float.h>
#include <complex.h>
#ifdef USE_ACCELERATE
#include <Accelerate/Accelerate.h>
#endif

#define N 24000
static uint64_t rd_fpcr(void) { return __builtin_arm_rsr64("fpcr"); }
static void wr_fpcr(uint64_t v) { __builtin_arm_wsr64("fpcr", v); }

static uint64_t s = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void) { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }

static float  X[N], Y[N], Z[N];
static double DX[N], DY[N], DZ[N];
static uint32_t IX[N];

static float fbits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static double dbits(uint64_t u) { double f; memcpy(&f, &u, 8); return f; }

static void gen(float *a, double *d, int seedmix) {
    static const uint32_t sp[] = {0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000, 0x7f800001, 0x7fa12345, 0x3f800000,
        0xbf800000, 0x3f000000, 0x40000000, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff, 0xff7fffff, 0x4b000000, 0x4f000000, 0x5f000000, 0x358637bd, 0x42b17218, 0xc2ce8ed0, 0xc2cff1b5};
    for (int i = 0; i < N; i++) {
        uint64_t r = rnd() ^ (uint64_t)seedmix;
        uint32_t u = (uint32_t)(r >> 32);
        float f;
        switch ((r >> 8) & 7) {
            case 0: case 1: f = fbits(u); break;                                  // uniform bits (NaN/Inf ~0.8%)
            case 2: f = ((float)(int32_t)u / 2147483648.0f) * 100.0f; break;      // [-100, 100]
            case 3: f = fbits(u & 0x807fffffu); break;                            // denormals / zero
            case 4: f = fbits(sp[(u >> 7) % (sizeof sp / sizeof sp[0])]); break;
            case 5: f = (float)((int32_t)(u >> 8) % 2001 - 1000) * 0.5f; break;   // halves
            case 6: f = ldexpf((float)((u & 0xffff) + 1) / 65536.0f, (int)((u >> 16) % 300) - 150); break;   // wide exponent range, incl. denormal results
            default: f = fbits((u & 0x807fffffu) | ((((u >> 23) & 0xff) % 40 + 100) << 23)); break;   // moderate exponents
        }
        a[i] = f;
        uint64_t q = rnd();
        double g;
        switch ((q >> 8) & 7) {
            case 0: case 1: g = dbits(q ^ (r << 7)); break;
            case 2: g = ((double)(int64_t)q / 9.223372036854775808e18) * 100.0; break;
            case 3: g = dbits(q & 0x800fffffffffffffull); break;
            case 4: g = (double)f; break;
            case 5: g = (double)((int32_t)(q >> 40) % 2001 - 1000) * 0.5; break;
            case 6: g = ldexp((double)((q & 0xffffff) + 1) / 16777216.0, (int)((q >> 24) % 2200) - 1100); break;
            default: g = dbits((q & 0x800fffffffffffffull) | ((((q >> 52) & 0x7ff) % 40 + 1000) << 52)); break;
        }
        d[i] = g;
    }
}

typedef struct { const char *name; int nargs; int dbl; float (*f1)(float); float (*f2)(float, float); double (*d1)(double); double (*d2)(double, double); } Fn;

#define W1F(n, e) static float  w_##n(float a) { return e; }
#define W2F(n, e) static float  w_##n(float a, float b) { return e; }
#define W1D(n, e) static double w_##n(double a) { return e; }
#define W2D(n, e) static double w_##n(double a, double b) { return e; }
// ordinary C expressions, compiled at the build's -O level
W2F(addf, a + b) W2F(subf, a - b) W2F(mulf, a * b) W2F(divf, a / b) W2F(minf_t, a < b ? a : b) W2F(maxf_t, a > b ? a : b)
W2D(addd, a + b) W2D(subd, a - b) W2D(muld, a * b) W2D(divd, a / b) W2D(mind_t, a < b ? a : b) W2D(maxd_t, a > b ? a : b)
W1F(f2d2f, (float)(double)a) W1D(d2f2d, (double)(float)a)
W1F(f2i_clamped, (float)(int)(a > 2e9f ? 2e9f : (a < -2e9f ? -2e9f : (a == a ? a : 0.0f))))
W1F(neg_abs, -fabsf(a))
W2F(muladd, a * b + 1.5f)         // may contract to fmadd
W2D(muladdd, a * b + 1.5)
W1F(absmaxzero, (a > 0.0f ? a : 0.0f))
// libm through a real call
#define LIBF1(n) static float w_l_##n(float a) { return n##f(a); }
#define LIBF2(n) static float w_l_##n(float a, float b) { return n##f(a, b); }
#define LIBD1(n) static double w_ld_##n(double a) { return n(a); }
#define LIBD2(n) static double w_ld_##n(double a, double b) { return n(a, b); }
LIBF1(sqrt) LIBF1(cbrt) LIBF1(exp) LIBF1(exp2) LIBF1(expm1) LIBF1(log) LIBF1(log2) LIBF1(log10) LIBF1(log1p) LIBF1(sin) LIBF1(cos) LIBF1(tan)
LIBF1(asin) LIBF1(acos) LIBF1(atan) LIBF1(sinh) LIBF1(cosh) LIBF1(tanh) LIBF1(asinh) LIBF1(acosh) LIBF1(atanh) LIBF1(floor) LIBF1(ceil)
LIBF1(trunc) LIBF1(round) LIBF1(rint) LIBF1(nearbyint) LIBF1(fabs) LIBF1(erf) LIBF1(erfc) LIBF1(tgamma) LIBF1(lgamma) LIBF1(logb)
LIBF2(pow) LIBF2(fmod) LIBF2(remainder) LIBF2(atan2) LIBF2(hypot) LIBF2(fmin) LIBF2(fmax) LIBF2(fdim) LIBF2(copysign) LIBF2(nextafter)
LIBD1(sqrt) LIBD1(cbrt) LIBD1(exp) LIBD1(exp2) LIBD1(expm1) LIBD1(log) LIBD1(log2) LIBD1(log10) LIBD1(log1p) LIBD1(sin) LIBD1(cos) LIBD1(tan)
LIBD1(asin) LIBD1(acos) LIBD1(atan) LIBD1(sinh) LIBD1(cosh) LIBD1(tanh) LIBD1(floor) LIBD1(ceil) LIBD1(trunc) LIBD1(round) LIBD1(rint)
LIBD1(nearbyint) LIBD1(erf) LIBD1(tgamma) LIBD1(asinh) LIBD1(acosh) LIBD1(atanh) LIBD1(erfc) LIBD1(lgamma) LIBD1(logb) LIBD1(fabs) LIBD1(j0) LIBD1(j1) LIBD1(y0) LIBD1(y1)
LIBD2(pow) LIBD2(fmod) LIBD2(remainder) LIBD2(atan2) LIBD2(hypot) LIBD2(fmin) LIBD2(fmax) LIBD2(fdim) LIBD2(copysign) LIBD2(nextafter)
static float w_l_fma(float a, float b) { return fmaf(a, b, 1.25f); }
static double w_ld_fma(double a, double b) { return fma(a, b, 1.25); }
static double w_ld_ldexp(double a, double b) { return ldexp(a, (int)(b > 3000 ? 3000 : (b < -3000 ? -3000 : (b == b ? b : 0)))); }
static double w_ld_frexp(double a, double b) { int e; double m = frexp(a, &e); (void)b; return m + (double)e; }
static double w_ld_modf(double a, double b) { double ip; double fr = modf(a, &ip); (void)b; return fr + ip; }
static double w_ld_lrint(double a, double b) { (void)b; return (a > -2e9 && a < 2e9) ? (double)lrint(a) : 0.0; }
static double w_ld_cabs(double a, double b) { double _Complex z = a + b * 1.0i; return cabs(z); }
static double w_ld_csqrt(double a, double b) { double _Complex z = a + b * 1.0i; return creal(csqrt(z)) + 3.0 * cimag(csqrt(z)); }
static double w_ld_cexp(double a, double b) { double _Complex z = (a > 50 ? 50 : (a < -50 ? -50 : a)) + b * 1.0i; return creal(cexp(z)) - cimag(cexp(z)); }
static double w_ld_neg(double a, double b) { (void)b; return -a; }
static double w_ld_fabs_c(double a, double b) { (void)b; return a < 0 ? -a : a; }
static float w_l_ldexp(float a, float b) { return ldexpf(a, (int)(b > 400 ? 400 : (b < -400 ? -400 : (b == b ? b : 0)))); }
static float w_l_frexp(float a, float b) { int e; float m = frexpf(a, &e); (void)b; return m + (float)e; }
static float w_l_modf(float a, float b) { float ip; float fr = modff(a, &ip); (void)b; return fr + ip; }
static float w_l_lrint(float a, float b) { (void)b; return (a > -2e9f && a < 2e9f) ? (float)lrintf(a) : 0.0f; }
static float w_l_lround(float a, float b) { (void)b; return (a > -2e9f && a < 2e9f) ? (float)lroundf(a) : 0.0f; }
static float w_l_cabs(float a, float b) { float _Complex z = a + b * 1.0fi; return cabsf(z); }

#define F1(n, f) { n, 1, 0, f, 0, 0, 0 }
#define F2(n, f) { n, 2, 0, 0, f, 0, 0 }
#define D1(n, f) { n, 1, 1, 0, 0, f, 0 }
#define D2(n, f) { n, 2, 1, 0, 0, 0, f }
static const Fn fns[] = {
    F2("C a+b float", w_addf), F2("C a-b float", w_subf), F2("C a*b float", w_mulf), F2("C a/b float", w_divf),
    F2("C a<b?a:b float", w_minf_t), F2("C a>b?a:b float", w_maxf_t), F2("C a*b+1.5 float", w_muladd), F1("C (float)(int)clamp float", w_f2i_clamped),
    F1("C -fabs float", w_neg_abs), F1("C a>0?a:0 float", w_absmaxzero), F1("C float->double->float", w_f2d2f),
    D2("C a+b double", w_addd), D2("C a-b double", w_subd), D2("C a*b double", w_muld), D2("C a/b double", w_divd),
    D2("C a<b?a:b double", w_mind_t), D2("C a>b?a:b double", w_maxd_t), D2("C a*b+1.5 double", w_muladdd), D1("C double->float->double", w_d2f2d),
    F1("sqrtf", w_l_sqrt), F1("cbrtf", w_l_cbrt), F1("expf", w_l_exp), F1("exp2f", w_l_exp2), F1("expm1f", w_l_expm1), F1("logf", w_l_log),
    F1("log2f", w_l_log2), F1("log10f", w_l_log10), F1("log1pf", w_l_log1p), F1("sinf", w_l_sin), F1("cosf", w_l_cos), F1("tanf", w_l_tan),
    F1("asinf", w_l_asin), F1("acosf", w_l_acos), F1("atanf", w_l_atan), F1("sinhf", w_l_sinh), F1("coshf", w_l_cosh), F1("tanhf", w_l_tanh),
    F1("asinhf", w_l_asinh), F1("acoshf", w_l_acosh), F1("atanhf", w_l_atanh), F1("floorf", w_l_floor), F1("ceilf", w_l_ceil), F1("truncf", w_l_trunc),
    F1("roundf", w_l_round), F1("rintf", w_l_rint), F1("nearbyintf", w_l_nearbyint), F1("fabsf", w_l_fabs), F1("erff", w_l_erf), F1("erfcf", w_l_erfc),
    F1("tgammaf", w_l_tgamma), F1("lgammaf", w_l_lgamma), F1("logbf", w_l_logb),
    F2("powf", w_l_pow), F2("fmodf", w_l_fmod), F2("remainderf", w_l_remainder), F2("atan2f", w_l_atan2), F2("hypotf", w_l_hypot),
    F2("fminf (libm)", w_l_fmin), F2("fmaxf (libm)", w_l_fmax), F2("fdimf", w_l_fdim), F2("copysignf", w_l_copysign), F2("nextafterf", w_l_nextafter),
    F2("fmaf", w_l_fma), F2("ldexpf", w_l_ldexp), F2("frexpf", w_l_frexp), F2("modff", w_l_modf), F2("lrintf", w_l_lrint), F2("lroundf", w_l_lround), F2("cabsf", w_l_cabs),
    D1("sqrt", w_ld_sqrt), D1("cbrt", w_ld_cbrt), D1("exp", w_ld_exp), D1("exp2", w_ld_exp2), D1("expm1", w_ld_expm1), D1("log", w_ld_log), D1("log2", w_ld_log2),
    D1("log10", w_ld_log10), D1("log1p", w_ld_log1p), D1("sin", w_ld_sin), D1("cos", w_ld_cos), D1("tan", w_ld_tan), D1("asin", w_ld_asin), D1("acos", w_ld_acos),
    D1("atan", w_ld_atan), D1("sinh", w_ld_sinh), D1("cosh", w_ld_cosh), D1("tanh", w_ld_tanh), D1("floor", w_ld_floor), D1("ceil", w_ld_ceil), D1("trunc", w_ld_trunc),
    D1("round", w_ld_round), D1("rint", w_ld_rint), D1("nearbyint", w_ld_nearbyint), D1("erf", w_ld_erf), D1("tgamma", w_ld_tgamma),
    D2("pow", w_ld_pow), D2("fmod", w_ld_fmod), D2("remainder", w_ld_remainder), D2("atan2", w_ld_atan2), D2("hypot", w_ld_hypot),
    D2("fmin (libm)", w_ld_fmin), D2("fmax (libm)", w_ld_fmax), D2("fdim", w_ld_fdim), D2("copysign", w_ld_copysign), D2("fma", w_ld_fma), D2("ldexp", w_ld_ldexp), D2("frexp", w_ld_frexp), D2("modf", w_ld_modf), D2("lrint", w_ld_lrint), D2("cabs", w_ld_cabs), D2("csqrt", w_ld_csqrt), D2("cexp", w_ld_cexp), D2("C -a double", w_ld_neg), D2("C a<0?-a:a double", w_ld_fabs_c),
    D1("asinh", w_ld_asinh), D1("acosh", w_ld_acosh), D1("atanh", w_ld_atanh), D1("erfc", w_ld_erfc), D1("lgamma", w_ld_lgamma), D1("logb", w_ld_logb), D1("fabs", w_ld_fabs), D1("j0", w_ld_j0), D1("j1", w_ld_j1), D1("y0", w_ld_y0), D1("y1", w_ld_y1), D2("nextafter", w_ld_nextafter),
};
#define NFN (sizeof fns / sizeof fns[0])

// ---------------- array kernels: what the compiler vectorises --------------------------------
#define K 16
typedef float (*kern)(const float *a, const float *b, int n);
#define NOINL __attribute__((noinline))
NOINL static float k_sum(const float *a, const float *b, int n)   { (void)b; float s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
NOINL static float k_dot(const float *a, const float *b, int n)   { float s = 0; for (int i = 0; i < n; i++) s += a[i] * b[i]; return s; }
NOINL static float k_max(const float *a, const float *b, int n)   { (void)b; float m = a[0]; for (int i = 1; i < n; i++) m = a[i] > m ? a[i] : m; return m; }
NOINL static float k_min(const float *a, const float *b, int n)   { (void)b; float m = a[0]; for (int i = 1; i < n; i++) m = a[i] < m ? a[i] : m; return m; }
NOINL static float k_fmaxf(const float *a, const float *b, int n) { (void)b; float m = a[0]; for (int i = 1; i < n; i++) m = fmaxf(m, a[i]); return m; }
NOINL static float k_norm(const float *a, const float *b, int n)  { (void)b; float s = 0; for (int i = 0; i < n; i++) s += a[i] * a[i]; return sqrtf(s); }
NOINL static float k_clamp(const float *a, const float *b, int n) { float s = 0; for (int i = 0; i < n; i++) { float v = a[i]; v = v < -1.0f ? -1.0f : v; v = v > b[i] ? b[i] : v; s += v; } return s; }
NOINL static float k_saxpy(const float *a, const float *b, int n) { float s = 0; for (int i = 0; i < n; i++) s += 2.5f * a[i] + b[i]; return s; }
NOINL static float k_div(const float *a, const float *b, int n)   { float s = 0; for (int i = 0; i < n; i++) s += a[i] / b[i]; return s; }
NOINL static float k_f2i(const float *a, const float *b, int n)   { (void)b; int s = 0; for (int i = 0; i < n; i++) { float v = a[i]; v = v > 1e9f ? 1e9f : v; v = v < -1e9f ? -1e9f : v; if (v == v) s += (int)v; } return (float)s; }
NOINL static float k_i2f(const float *a, const float *b, int n)   { (void)b; float s = 0; for (int i = 0; i < n; i++) s += (float)(int)(a[i] > 1e9f ? 1e9f : (a[i] < -1e9f ? -1e9f : (a[i] == a[i] ? a[i] : 0))); return s; }
NOINL static float k_round(const float *a, const float *b, int n) { (void)b; float s = 0; for (int i = 0; i < n; i++) s += floorf(a[i] + 0.5f); return s; }
NOINL static float k_cmpcount(const float *a, const float *b, int n) { int c = 0; for (int i = 0; i < n; i++) c += (a[i] < b[i]) + 2 * (a[i] == b[i]) + 4 * (a[i] != a[i]); return (float)c; }
// integer bit operations on float bit patterns: sign masks built by the compiler, not read from memory
NOINL static void b_and64(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; for (int i = 0; i < n; i++) o[i] = x[i] & 0x7fffffffffffffffull; }
NOINL static void b_xor64(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; for (int i = 0; i < n; i++) o[i] = x[i] ^ 0x8000000000000000ull; }
NOINL static void b_or64 (const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; for (int i = 0; i < n; i++) o[i] = x[i] | 0x8000000000000000ull; }
NOINL static void b_and32(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; const uint32_t *a = (const uint32_t *)x; uint32_t *r = (uint32_t *)o; for (int i = 0; i < 2 * n; i++) r[i] = a[i] & 0x7fffffffu; }
NOINL static void b_xor32(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; const uint32_t *a = (const uint32_t *)x; uint32_t *r = (uint32_t *)o; for (int i = 0; i < 2 * n; i++) r[i] = a[i] ^ 0x80000000u; }
NOINL static void b_cpsign64(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { for (int i = 0; i < n; i++) { double a, b; memcpy(&a, &x[i], 8); memcpy(&b, &y[i], 8); double r = copysign(a, b); memcpy(&o[i], &r, 8); } }
NOINL static void b_fabs64(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; for (int i = 0; i < n; i++) { double a; memcpy(&a, &x[i], 8); double r = fabs(a); memcpy(&o[i], &r, 8); } }
NOINL static void b_neg64(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; for (int i = 0; i < n; i++) { double a; memcpy(&a, &x[i], 8); double r = -a; memcpy(&o[i], &r, 8); } }
NOINL static void b_cpsign32(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { const float *a = (const float *)x, *b = (const float *)y; float *r = (float *)o; for (int i = 0; i < 2 * n; i++) r[i] = copysignf(a[i], b[i]); }
NOINL static void b_fabs32(const uint64_t *x, const uint64_t *y, uint64_t *o, int n) { (void)y; const float *a = (const float *)x; float *r = (float *)o; for (int i = 0; i < 2 * n; i++) r[i] = fabsf(a[i]); }
typedef void (*bkern)(const uint64_t *, const uint64_t *, uint64_t *, int);
typedef struct { const char *name; bkern k; int view; } BKern;     /* view: 0 integer bits, 1 doubles, 2 floats */
static const BKern bkerns[] = { {"bits x & 0x7fff..(64)", b_and64, 0}, {"bits x ^ 0x8000..(64)", b_xor64, 0}, {"bits x OR 0x8000..(64)", b_or64, 0}, {"bits x & 0x7fffffff(32)", b_and32, 0},
    {"bits x ^ 0x80000000(32)", b_xor32, 0}, {"loop copysign(double)", b_cpsign64, 1}, {"loop fabs(double)", b_fabs64, 1}, {"loop -x (double)", b_neg64, 1},
    {"loop copysignf", b_cpsign32, 2}, {"loop fabsf", b_fabs32, 2} };
#define NBK (sizeof bkerns / sizeof bkerns[0])
typedef struct { const char *name; kern k; } Kern;
static const Kern kerns[] = { {"loop sum", k_sum}, {"loop dot a*b", k_dot}, {"loop max a>m?a:m", k_max}, {"loop min a<m?a:m", k_min}, {"loop fmaxf", k_fmaxf},
    {"loop norm sqrtf", k_norm}, {"loop clamp", k_clamp}, {"loop 2.5a+b", k_saxpy}, {"loop a/b", k_div}, {"loop (int)f", k_f2i}, {"loop (float)int", k_i2f},
    {"loop floor(a+.5)", k_round}, {"loop compare counts", k_cmpcount} };
#define NKERN (sizeof kerns / sizeof kerns[0])

// ---------------- results -----------------------------------------------------------------------
#define NM 4
static const char *mname[NM] = {"FPCR=0", "AH", "AH+NEP", "AH+FIZ+FZ"};
static uint64_t mval[NM];
static uint64_t R[NM][N];
static uint64_t IN1[N], IN2[N];     /* input bits of the element being compared, for the report */

static int isnan64(int dbl, uint64_t b) { return dbl ? ((b >> 52) & 0x7ff) == 0x7ff && (b & 0xfffffffffffffull) : ((b >> 23) & 0xff) == 0xff && (b & 0x7fffffu); }
static int in_nan(const Fn *f, int i) {
    if (f->dbl) return isnan(DX[i]) || (f->nargs == 2 && isnan(DY[i]));
    return isnan(X[i]) || (f->nargs == 2 && isnan(Y[i]));
}

typedef struct { long total[NM], nanin[NM], nanres[NM], real[NM]; char ex[NM][3][160]; int nex[NM]; } Tally;
static void compare(const char *name, int dbl, int n, int (*innan)(const void *, int), const void *ctx, Tally *t) {
    for (int m = 1; m < NM; m++) for (int i = 0; i < n; i++) {
        t->total[m]++;
        if (R[m][i] == R[0][i]) continue;
        if (innan(ctx, i)) t->nanin[m]++;
        else if (isnan64(dbl, R[m][i]) || isnan64(dbl, R[0][i])) t->nanres[m]++;
        else {
            t->real[m]++;
            if (t->nex[m] < 3) { snprintf(t->ex[m][t->nex[m]++], 160, "%-20s in=%llx,%llx  FPCR=0 -> %llx   %s -> %llx", name, (unsigned long long)IN1[i], (unsigned long long)IN2[i], (unsigned long long)R[0][i], mname[m], (unsigned long long)R[m][i]); }
        }
    }
}
static int innan_fn(const void *c, int i) { return in_nan((const Fn *)c, i); }
static int innan_blk(const void *c, int i) { (void)c; for (int j = 0; j < K; j++) if (isnan(X[i * K + j]) || isnan(Y[i * K + j])) return 1; return 0; }
static int innan_dview(const void *c, int i) { (void)c; return isnan(DX[i]) || isnan(DY[i]); }
static int innan_fview(const void *c, int i) { (void)c; float a[2], b[2]; memcpy(a, &DX[i], 8); memcpy(b, &DY[i], 8); return isnan(a[0]) || isnan(a[1]) || isnan(b[0]) || isnan(b[1]); }
static int innan_none(const void *c, int i) { (void)c; (void)i; return 0; }
static int innan_str(const void *c, int i) { (void)c; return isnan(X[i]); }

static void report(const char *name, const Tally *t, int *any_real) {
    printf("%-26s", name);
    for (int m = 1; m < NM; m++) {
        printf(" | %-10s same=%5.1f%% nan-in=%-5ld nan-res=%-5ld REAL=%-5ld", mname[m], 100.0 * (t->total[m] - t->nanin[m] - t->nanres[m] - t->real[m]) / (double)t->total[m], t->nanin[m], t->nanres[m], t->real[m]);
        if (m == 2) m = NM - 2;     /* print AH and AH+FIZ+FZ only; NEP is checked separately */
    }
    printf("\n");
    for (int m = 1; m < NM; m++) for (int e = 0; e < t->nex[m]; e++) printf("      REAL under %s: %s\n", mname[m], t->ex[m][e]);
    for (int m = 1; m < 3; m++) if (t->real[m]) *any_real = 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t base = rd_fpcr();
    mval[0] = base; mval[1] = base | 2; mval[2] = base | 2 | 4; mval[3] = base | 2 | 1 | (1u << 24);
    gen(X, DX, 0); gen(Y, DY, 0x55); gen(Z, DZ, 0xaa);
    for (int i = 0; i < N; i++) IX[i] = (uint32_t)rnd();
    int any_real = 0;
    printf("# FPCR at start %#llx; N=%d inputs per function; modes: ", (unsigned long long)base, N);
    for (int m = 0; m < NM; m++) printf("%s=%#llx ", mname[m], (unsigned long long)mval[m]);
    printf("\n");

    for (size_t f = 0; f < NFN; f++) {
        const Fn *fn = &fns[f];
        Tally *t = calloc(1, sizeof *t);
        for (int m = 0; m < NM; m++) {
            wr_fpcr(mval[m]);
            for (int i = 0; i < N; i++) {
                if (fn->dbl) { double r = fn->nargs == 1 ? fn->d1(DX[i]) : fn->d2(DX[i], DY[i]); memcpy(&R[m][i], &r, 8); }
                else { float r = fn->nargs == 1 ? fn->f1(X[i]) : fn->f2(X[i], Y[i]); R[m][i] = 0; memcpy(&R[m][i], &r, 4); }
            }
            wr_fpcr(base);
        }
        for (int i = 0; i < N; i++) { if (fn->dbl) { memcpy(&IN1[i], &DX[i], 8); memcpy(&IN2[i], &DY[i], 8); } else { IN1[i] = IN2[i] = 0; memcpy(&IN1[i], &X[i], 4); memcpy(&IN2[i], &Y[i], 4); } }
        compare(fn->name, fn->dbl, N, innan_fn, fn, t);
        report(fn->name, t, &any_real);
        free(t);
    }
    for (size_t k = 0; k < NKERN; k++) {
        Tally *t = calloc(1, sizeof *t);
        for (int m = 0; m < NM; m++) {
            wr_fpcr(mval[m]);
            for (int i = 0; i < N / K; i++) { float r = kerns[k].k(&X[i * K], &Y[i * K], K); R[m][i] = 0; memcpy(&R[m][i], &r, 4); }
            wr_fpcr(base);
        }
        for (int i = 0; i < N / K; i++) { IN1[i] = IN2[i] = 0; memcpy(&IN1[i], &X[i * K], 4); memcpy(&IN2[i], &Y[i * K], 4); }
        compare(kerns[k].name, 0, N / K, innan_blk, NULL, t);
        report(kerns[k].name, t, &any_real);
        free(t);
    }
    for (size_t k = 0; k < NBK; k++) {
        static uint64_t o[NM][N];
        Tally *t = calloc(1, sizeof *t);
        uint64_t xb[N], yb[N];
        for (int i = 0; i < N; i++) { memcpy(&xb[i], &DX[i], 8); memcpy(&yb[i], &DY[i], 8); }
        for (int m = 0; m < NM; m++) { wr_fpcr(mval[m]); bkerns[k].k(xb, yb, o[m], N); wr_fpcr(base); memcpy(R[m], o[m], sizeof R[m]); }
        for (int i = 0; i < N; i++) { IN1[i] = xb[i]; IN2[i] = yb[i]; }
        compare(bkerns[k].name, 1, N, bkerns[k].view == 0 ? innan_none : (bkerns[k].view == 1 ? innan_dview : innan_fview), NULL, t);
        report(bkerns[k].name, t, &any_real);
        free(t);
    }
    // string conversion both ways
    {
        for (int i = 0; i < N; i++) { IN1[i] = IN2[i] = 0; memcpy(&IN1[i], &X[i], 4); }
        Tally *t = calloc(1, sizeof *t);
        for (int m = 0; m < NM; m++) {
            wr_fpcr(mval[m]);
            for (int i = 0; i < N; i++) { char buf[64]; snprintf(buf, sizeof buf, "%.9g", (double)X[i]); uint64_t h = 1469598103934665603ull; for (char *p = buf; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull; R[m][i] = h; }
            wr_fpcr(base);
        }
        compare("snprintf %.9g (hash)", 0, N, innan_str, NULL, t); report("snprintf %.9g (hash)", t, &any_real); free(t);
        t = calloc(1, sizeof *t);
        for (int m = 0; m < NM; m++) {
            char buf[64]; wr_fpcr(base);
            for (int i = 0; i < N; i++) { snprintf(buf, sizeof buf, "%.9g", (double)X[i]); wr_fpcr(mval[m]); float r = strtof(buf, NULL); wr_fpcr(base); R[m][i] = 0; memcpy(&R[m][i], &r, 4); }
        }
        compare("strtof(%.9g)", 0, N, innan_str, NULL, t); report("strtof(%.9g)", t, &any_real); free(t);
        t = calloc(1, sizeof *t);
        for (int m = 0; m < NM; m++) {
            char buf[64]; wr_fpcr(base);
            for (int i = 0; i < N; i++) { snprintf(buf, sizeof buf, "%.17g", DX[i]); wr_fpcr(mval[m]); double r = strtod(buf, NULL); wr_fpcr(base); memcpy(&R[m][i], &r, 8); }
        }
        compare("strtod(%.17g)", 1, N, innan_str, NULL, t); report("strtod(%.17g)", t, &any_real); free(t);
    }
#ifdef USE_ACCELERATE
    {   // a system framework doing its own NEON: vForce, vDSP, BLAS
        static float out[N];
        Tally *t;
        #define ACC(label, ...) t = calloc(1, sizeof *t); \
            for (int m = 0; m < NM; m++) { wr_fpcr(mval[m]); __VA_ARGS__; wr_fpcr(base); for (int i = 0; i < N; i++) { R[m][i] = 0; memcpy(&R[m][i], &out[i], 4); } } \
            compare(label, 0, N, innan_str, NULL, t); report(label, t, &any_real); free(t);
        int n = N;
        for (int i = 0; i < N; i++) { IN1[i] = IN2[i] = 0; memcpy(&IN1[i], &X[i], 4); memcpy(&IN2[i], &Y[i], 4); }
        ACC("vvsqrtf", vvsqrtf(out, X, &n))
        ACC("vvexpf", vvexpf(out, X, &n))
        ACC("vvsinf", vvsinf(out, X, &n))
        ACC("vvlogf", vvlogf(out, X, &n))
        ACC("vvrecf", vvrecf(out, X, &n))
        ACC("vDSP_vadd", vDSP_vadd(X, 1, Y, 1, out, 1, N))
        ACC("vDSP_vmul", vDSP_vmul(X, 1, Y, 1, out, 1, N))
        ACC("vDSP_vdiv", vDSP_vdiv(Y, 1, X, 1, out, 1, N))
        ACC("vDSP_vmax", vDSP_vmax(X, 1, Y, 1, out, 1, N))
        ACC("vDSP_vmin", vDSP_vmin(X, 1, Y, 1, out, 1, N))
        ACC("vDSP_vclip", { float lo = -1.0f, hi = 1.0f; vDSP_vclip(X, 1, &lo, &hi, out, 1, N); })
        ACC("vDSP_vabs", vDSP_vabs(X, 1, out, 1, N))
        ACC("vDSP_vsq", vDSP_vsq(X, 1, out, 1, N))
        ACC("vDSP_vsmul", { float sc = 0.3f; vDSP_vsmul(X, 1, &sc, out, 1, N); })
        ACC("vDSP_sve", { for (int i = 0; i < N; i++) out[i] = 0; vDSP_sve(X, 1, &out[0], N); })
        ACC("vDSP_maxv", { for (int i = 0; i < N; i++) out[i] = 0; vDSP_maxv(X, 1, &out[0], N); })
        ACC("vDSP_minv", { for (int i = 0; i < N; i++) out[i] = 0; vDSP_minv(X, 1, &out[0], N); })
        ACC("vDSP_dotpr", { for (int i = 0; i < N; i++) out[i] = 0; vDSP_dotpr(X, 1, Y, 1, &out[0], N); })
        ACC("cblas_sgemm 150x160", { for (int i = 0; i < N; i++) out[i] = 0; cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, 150, 150, 160, 1.0f, X, 160, Y, 150, 0.0f, out, 150); })
    }
#endif
    printf("RESULT %s\n", any_real ? "REAL-DIFFERENCES-FOUND" : "no-real-differences");
    return 0;
}
