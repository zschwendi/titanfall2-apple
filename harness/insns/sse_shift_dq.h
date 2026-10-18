/* C helpers of the sse_shift_dq family: psrldq and pslldq, shifts of a whole xmm register by
   n bytes. n is imm8 & 0xFF; n above 15 gives all zeros. */
#ifndef TF_INSNS_SSE_SHIFT_DQ_H
#define TF_INSNS_SSE_SHIFT_DQ_H

#include "../runtime.h"

/* psrldq: r.b[k] = (k + n < 16) ? s.b[k + n] : 0 */
TF_INLINE Xmm x_psrldq(Xmm s, uint32_t n) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (uint32_t k = 0; k < 16 && k + n < 16; k++)
        r.b[k] = s.b[k + n];
    return r;
}

/* pslldq: r.b[k] = (k >= n) ? s.b[k - n] : 0 */
TF_INLINE Xmm x_pslldq(Xmm s, uint32_t n) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (uint32_t k = n; k < 16; k++)
        r.b[k] = s.b[k - n];
    return r;
}

#endif
