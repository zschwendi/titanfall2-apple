/* cvtss2si, cvtsd2si: the range test of the rounding conversions (sse_cvt_s2si.py).

   The caller has rounded the source already (tf_frint32 or tf_frint64, which follow MXCSR.RC). The rounded
   value is exact in a double, so one test per destination width serves both source sizes. NaN and
   every value outside the integer range give the integer indefinite, as x86 does. */
#ifndef TF_INSNS_SSE_CVT_S2SI_H
#define TF_INSNS_SSE_CVT_S2SI_H

#include "../runtime.h"

TF_INLINE int32_t x_sse_cvt_s2si_32(double t) {
    return (t >= -2147483648.0 && t < 2147483648.0) ? (int32_t)t : INT32_MIN;
}

TF_INLINE int64_t x_sse_cvt_s2si_64(double t) {
    return (t >= -9223372036854775808.0 && t < 9223372036854775808.0) ? (int64_t)t : INT64_MIN;
}

#endif
