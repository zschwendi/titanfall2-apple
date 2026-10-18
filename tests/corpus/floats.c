/* Scalar and packed floating point: arithmetic, compares, conversions. */
#include "corpus.h"

EXPORT float f_arith(float a, float b) { return (a + b) * (a - b) / (b * b + 1.0f); }
EXPORT double d_arith(double a, double b) { return (a * b - a) / (b + 2.0) + a; }
EXPORT float f_sqrt(float a) { return __builtin_sqrtf(a < 0 ? -a : a); }
EXPORT float f_min(float a, float b) { return a < b ? a : b; }
EXPORT float f_max(float a, float b) { return a > b ? a : b; }
EXPORT float f_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
EXPORT i32 f_cmp(float a, float b) { return (a < b) | ((a == b) << 1) | ((a > b) << 2) | ((a != a || b != b) << 3); }
EXPORT i32 d_cmp(double a, double b) { return (a <= b) | ((a >= b) << 1) | ((a != b) << 2); }
EXPORT i32 f_to_i(float a) { return a > -1.0e9f && a < 1.0e9f ? (i32)a : 0; }
EXPORT i64 d_to_i64(double a) { return a > -1.0e18 && a < 1.0e18 ? (i64)a : 0; }
EXPORT float i_to_f(i32 a) { return (float)a * 0.5f; }
EXPORT double u_to_d(u32 a) { return (double)a / 3.0; }
EXPORT double f_to_d(float a) { return (double)a * 1.0000001; }
EXPORT float d_to_f(double a) { return (float)(a * 0.999); }
EXPORT float lerp(float a, float b, float t) { return a + (b - a) * t; }
EXPORT float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
EXPORT void cross3(float *o, const float *a, const float *b) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
EXPORT void scale4(float *v, float s) { for (int i = 0; i < 4; i++) v[i] *= s; }
EXPORT void add_arrays(float *o, const float *a, const float *b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i] * 0.25f; }
EXPORT float sum_array(const float *a, u32 n) { float s = 0; n &= 31; for (u32 i = 0; i < n; i++) s += a[i]; return s; }
EXPORT void mat4_mul_vec(float *o, const float *m, const float *v) { for (int i = 0; i < 4; i++) o[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3]; }
EXPORT float poly(float x) { return ((0.5f * x - 1.25f) * x + 3.0f) * x - 0.125f; }
EXPORT float newton_rsqrt(float x) { if (!(x > 0.0f)) return 0.0f; float y = 1.0f; for (int i = 0; i < 6; i++) y = y * (1.5f - 0.5f * x * y * y); return y; }
EXPORT u32 float_bits(float a) { u32 b; memcpy(&b, &a, 4); return b; }
EXPORT float bits_float(u32 b) { float a; memcpy(&a, &b, 4); return a; }
EXPORT float f_abs_neg(float a) { return (a < 0 ? -a : a) - (-a); }
EXPORT void int_to_float_array(float *o, const i32 *v) { for (int i = 0; i < 8; i++) o[i] = (float)v[i]; }
EXPORT void float_to_int_array(i32 *o, const float *v) { for (int i = 0; i < 8; i++) { float f = v[i]; o[i] = f > -1.0e9f && f < 1.0e9f ? (i32)f : 0; } }
