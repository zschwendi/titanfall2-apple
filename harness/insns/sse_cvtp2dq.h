/* SSE packed float to signed 32-bit integer: cvttps2dq, cvttpd2dq (truncating) and cvtps2dq,
 * cvtpd2dq (rounding).
 *
 * Not (int)v: for NaN and out of range the x86 result is the integer indefinite, INT32_MIN.
 * The truncating bounds are the ones measured exact in both float modes (docs/findings/11-float-mode.md).
 *
 * The rounding forms round first, in the rounding mode (tf_frint32/64: the host frintx, FPCR.RMode,
 * as cvtss2si does), and range-test the rounded value: 2147483647.5 rounds to 2147483648, which is
 * out of range under round-to-nearest, as the CPU answers.
 */
#ifndef TF_INSNS_SSE_CVTP2DQ_H
#define TF_INSNS_SSE_CVTP2DQ_H
#include "../runtime.h"

TF_INLINE int32_t x_sse_cvtp2dq_t32_f(float v) {
    return (v >= -2147483648.0f && v < 2147483648.0f) ? (int32_t)v : INT32_MIN;
}

TF_INLINE int32_t x_sse_cvtp2dq_t32_d(double v) {
    return (v > -2147483649.0 && v < 2147483648.0) ? (int32_t)v : INT32_MIN;
}

TF_INLINE int32_t x_sse_cvtp2dq_r32_f(float v) {
    float t = tf_frint32(v);
    return (t >= -2147483648.0f && t < 2147483648.0f) ? (int32_t)t : INT32_MIN;
}

TF_INLINE int32_t x_sse_cvtp2dq_r32_d(double v) {
    double t = tf_frint64(v);
    return (t >= -2147483648.0 && t < 2147483648.0) ? (int32_t)t : INT32_MIN;
}

#endif
