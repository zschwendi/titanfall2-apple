/* Scalar single and double precision: arithmetic, min/max, reciprocal estimates, compares
   and conversions to and from integers. */
#include "corpus.h"
#include <emmintrin.h>

/* ---- single precision arithmetic ---- */
EXPORT float fs_addss_rr(float a, float b) { return a + b; }
EXPORT float fs_addss_mem(const float *p, float a) { return a + p[0]; }
EXPORT float fs_addss_const(float a) { return a + 1.5f; }
EXPORT float fs_subss_rr(float a, float b) { return a - b; }
EXPORT float fs_subss_mem(const float *p, float a) { return a - p[1]; }
EXPORT float fs_mulss_rr(float a, float b) { return a * b; }
EXPORT float fs_mulss_mem(const float *p, float a) { return a * p[2]; }
EXPORT float fs_mulss_const(float a) { return a * 0.1f; }
EXPORT float fs_divss_rr(float a, float b) { return a / b; }
EXPORT float fs_divss_mem(const float *p, float a) { return a / p[0]; }
EXPORT float fs_sqrtss_rr(float a) { return __builtin_sqrtf(a); }
EXPORT float fs_sqrtss_intrin(float a) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(a))); }
EXPORT float fs_sqrtss_mem(const float *p) { return __builtin_sqrtf(p[3]); }
EXPORT float fs_minss_rr(float a, float b) { return a < b ? a : b; }
EXPORT float fs_minss_intrin(float a, float b) { return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(a), _mm_set_ss(b))); }
EXPORT float fs_minss_mem(const float *p, float a) { return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(a), _mm_load_ss(p))); }
EXPORT float fs_maxss_rr(float a, float b) { return a > b ? a : b; }
EXPORT float fs_maxss_const(float a) { return a > 2.5f ? a : 2.5f; }

/* ---- double precision arithmetic ---- */
EXPORT double fs_addsd_rr(double a, double b) { return a + b; }
EXPORT double fs_addsd_mem(const double *p, double a) { return a + p[0]; }
EXPORT double fs_addsd_const(double a) { return a + 0.3; }
EXPORT double fs_subsd_rr(double a, double b) { return a - b; }
EXPORT double fs_subsd_mem(const double *p, double a) { return a - p[1]; }
EXPORT double fs_mulsd_rr(double a, double b) { return a * b; }
EXPORT double fs_mulsd_const(double a) { return a * 0.1; }
EXPORT double fs_divsd_rr(double a, double b) { return a / b; }
EXPORT double fs_divsd_mem(const double *p, double a) { return a / p[0]; }
EXPORT double fs_sqrtsd_rr(double a) { return __builtin_sqrt(a); }
EXPORT double fs_sqrtsd_intrin(double a) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(a), _mm_set_sd(a))); }
EXPORT double fs_minsd_rr(double a, double b) { return a < b ? a : b; }
EXPORT double fs_minsd_intrin(double a, double b) { return _mm_cvtsd_f64(_mm_min_sd(_mm_set_sd(a), _mm_set_sd(b))); }
EXPORT double fs_maxsd_rr(double a, double b) { return a > b ? a : b; }
EXPORT double fs_maxsd_mem(const double *p, double a) { return _mm_cvtsd_f64(_mm_max_sd(_mm_set_sd(a), _mm_load_sd(p))); }

/* ---- unordered compares: ucomiss / ucomisd ---- */
EXPORT i32 fs_ucomiss_lt(float a, float b) { return a < b; }
EXPORT i32 fs_ucomiss_eq(float a, float b) { return a == b; }
EXPORT i32 fs_ucomiss_ne(float a, float b) { return a != b; }
EXPORT i32 fs_ucomiss_nan(float a) { return a != a; }
EXPORT i32 fs_ucomiss_mem(const float *p, float a) { return a < p[0]; }
EXPORT i32 fs_ucomiss_const(float a) { return a > 1.0f; }
EXPORT i32 fs_ucomiss_branch(float a, float b) { if (a < b) return 1; if (a > b) return 2; return 0; }
EXPORT i32 fs_ucomisd_lt(double a, double b) { return a < b; }
EXPORT i32 fs_ucomisd_eq(double a, double b) { return a == b; }
EXPORT i32 fs_ucomisd_ne(double a, double b) { return a != b; }
EXPORT i32 fs_ucomisd_mem(const double *p, double a) { return a < p[0]; }
EXPORT i32 fs_ucomisd_branch(double a, double b) { if (a < b) return 1; if (a > b) return 2; return 0; }

/* ---- ordered compares: comiss / comisd through the intrinsics ---- */
EXPORT i32 fs_comiss_lt(float a, float b) { return _mm_comilt_ss(_mm_set_ss(a), _mm_set_ss(b)); }
EXPORT i32 fs_comiss_le(float a, float b) { return _mm_comile_ss(_mm_set_ss(a), _mm_set_ss(b)); }
EXPORT i32 fs_comiss_gt(float a, float b) { return _mm_comigt_ss(_mm_set_ss(a), _mm_set_ss(b)); }
EXPORT i32 fs_comiss_eq(float a, float b) { return _mm_comieq_ss(_mm_set_ss(a), _mm_set_ss(b)); }
EXPORT i32 fs_comiss_ne(float a, float b) { return _mm_comineq_ss(_mm_set_ss(a), _mm_set_ss(b)); }
EXPORT i32 fs_comiss_mem(const float *p, float a) { return _mm_comilt_ss(_mm_load_ss(p), _mm_set_ss(a)); }
EXPORT i32 fs_comisd_lt(double a, double b) { return _mm_comilt_sd(_mm_set_sd(a), _mm_set_sd(b)); }
EXPORT i32 fs_comisd_le(double a, double b) { return _mm_comile_sd(_mm_set_sd(a), _mm_set_sd(b)); }
EXPORT i32 fs_comisd_gt(double a, double b) { return _mm_comigt_sd(_mm_set_sd(a), _mm_set_sd(b)); }
EXPORT i32 fs_comisd_eq(double a, double b) { return _mm_comieq_sd(_mm_set_sd(a), _mm_set_sd(b)); }

/* ---- reciprocal estimates ---- */
EXPORT float fs_rcpss_rr(float a) { return _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(a))); }
EXPORT float fs_rcpss_mem(const float *p) { return _mm_cvtss_f32(_mm_rcp_ss(_mm_load_ss(p))); }
EXPORT float fs_rsqrtss_rr(float a) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(a))); }
EXPORT float fs_rsqrtss_mem(const float *p) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_load_ss(p))); }
EXPORT float fs_rsqrt_nr(float x) { float y = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x))); return y * (1.5f - 0.5f * x * y * y); }

/* ---- compare predicates: cmp*ss / cmp*sd, result is the low lane's mask bits ---- */
#define CMP_SS(name) \
EXPORT i32 fs_##name##ss_rr(float a, float b) { return _mm_cvtsi128_si32(_mm_castps_si128(_mm_##name##_ss(_mm_set_ss(a), _mm_set_ss(b)))); } \
EXPORT i32 fs_##name##ss_mem(const float *p, float a) { return _mm_cvtsi128_si32(_mm_castps_si128(_mm_##name##_ss(_mm_set_ss(a), _mm_load_ss(p)))); }
#define CMP_SD(name) \
EXPORT i64 fs_##name##sd_rr(double a, double b) { return _mm_cvtsi128_si64(_mm_castpd_si128(_mm_##name##_sd(_mm_set_sd(a), _mm_set_sd(b)))); } \
EXPORT i64 fs_##name##sd_mem(const double *p, double a) { return _mm_cvtsi128_si64(_mm_castpd_si128(_mm_##name##_sd(_mm_set_sd(a), _mm_load_sd(p)))); }

CMP_SS(cmpeq)
CMP_SS(cmplt)
CMP_SS(cmple)
CMP_SS(cmpunord)
CMP_SS(cmpneq)
CMP_SS(cmpnlt)
CMP_SS(cmpnle)
CMP_SS(cmpord)
CMP_SD(cmpeq)
CMP_SD(cmplt)
CMP_SD(cmple)
CMP_SD(cmpunord)
CMP_SD(cmpneq)
CMP_SD(cmpnlt)
CMP_SD(cmpnle)
CMP_SD(cmpord)

/* ---- integer to float: cvtsi2ss / cvtsi2sd, 32 and 64 bit, register and memory ---- */
EXPORT float fs_cvtsi2ss_r32(i32 a) { return (float)a; }
EXPORT float fs_cvtsi2ss_r64(i64 a) { return (float)a; }
EXPORT float fs_cvtsi2ss_m32(const i32 *p) { return (float)p[0]; }
EXPORT float fs_cvtsi2ss_m64(const i64 *p) { return (float)p[1]; }
EXPORT float fs_cvtsi2ss_u64(u64 a) { return (float)a; }
EXPORT double fs_cvtsi2sd_r32(i32 a) { return (double)a; }
EXPORT double fs_cvtsi2sd_r64(i64 a) { return (double)a; }
EXPORT double fs_cvtsi2sd_m64(const i64 *p) { return (double)p[0]; }

/* ---- float to integer, truncating: guarded casts, then the unguarded intrinsics ---- */
EXPORT i32 fs_cvttss2si_r32(float a) { return a > -2.0e9f && a < 2.0e9f ? (i32)a : 0; }
EXPORT i64 fs_cvttss2si_r64(float a) { return a > -9.0e18f && a < 9.0e18f ? (i64)a : 0; }
EXPORT i32 fs_cvttss2si_m32(const float *p) { return p[0] > -2.0e9f && p[0] < 2.0e9f ? (i32)p[0] : 0; }
EXPORT i32 fs_cvttss2si_intrin32(float a) { return _mm_cvttss_si32(_mm_set_ss(a)); }
EXPORT i64 fs_cvttss2si_intrin64(float a) { return _mm_cvttss_si64(_mm_set_ss(a)); }
EXPORT i32 fs_cvttsd2si_r32(double a) { return a > -2.0e9 && a < 2.0e9 ? (i32)a : 0; }
EXPORT i64 fs_cvttsd2si_r64(double a) { return a > -9.0e18 && a < 9.0e18 ? (i64)a : 0; }
EXPORT i64 fs_cvttsd2si_m64(const double *p) { return p[0] > -9.0e18 && p[0] < 9.0e18 ? (i64)p[0] : 0; }
EXPORT i64 fs_cvttsd2si_intrin64(double a) { return _mm_cvttsd_si64(_mm_set_sd(a)); }

/* ---- float to integer, rounding with MXCSR: cvtss2si / cvtsd2si ---- */
EXPORT i32 fs_cvtss2si_r32(float a) { return _mm_cvtss_si32(_mm_set_ss(a)); }
EXPORT i64 fs_cvtss2si_r64(float a) { return _mm_cvtss_si64(_mm_set_ss(a)); }
EXPORT i32 fs_cvtss2si_m32(const float *p) { return _mm_cvtss_si32(_mm_load_ss(p)); }
EXPORT i64 fs_cvtss2si_m64(const float *p) { return _mm_cvtss_si64(_mm_load_ss(p)); }
EXPORT i32 fs_cvtsd2si_r32(double a) { return _mm_cvtsd_si32(_mm_set_sd(a)); }
EXPORT i64 fs_cvtsd2si_r64(double a) { return _mm_cvtsd_si64(_mm_set_sd(a)); }
EXPORT i32 fs_cvtsd2si_m32(const double *p) { return _mm_cvtsd_si32(_mm_load_sd(p)); }

/* ---- precision conversions: cvtss2sd / cvtsd2ss ---- */
EXPORT double fs_cvtss2sd_rr(float a) { return (double)a; }
EXPORT double fs_cvtss2sd_mem(const float *p) { return (double)p[0]; }
EXPORT double fs_cvtss2sd_merge(double hi, float a) { return _mm_cvtsd_f64(_mm_cvtss_sd(_mm_set_sd(hi), _mm_set_ss(a))); }
EXPORT float fs_cvtsd2ss_rr(double a) { return (float)a; }
EXPORT float fs_cvtsd2ss_mem(const double *p) { return (float)p[0]; }
EXPORT float fs_cvtsd2ss_merge(float hi, double a) { return _mm_cvtss_f32(_mm_cvtsd_ss(_mm_set_ss(hi), _mm_set_sd(a))); }

/* ---- small loops and compositions ---- */
EXPORT float fs_sum_ss(const float *p, u32 n) { float s = 0.0f; n &= 15; for (u32 i = 0; i < n; i++) s += p[i]; return s; }
EXPORT double fs_dot_sd(const double *a, const double *b, u32 n) { double s = 0.0; n &= 15; for (u32 i = 0; i < n; i++) s += a[i] * b[i]; return s; }
EXPORT double fs_clamp_sd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
EXPORT i64 fs_roundtrip_i64(i64 v) { return _mm_cvtsd_si64(_mm_set_sd((double)v)); }
EXPORT float fs_max_array_ss(const float *p, u32 n) { float m = p[0]; n &= 15; for (u32 i = 1; i < n; i++) m = p[i] > m ? p[i] : m; return m; }
