/* C helpers of the sse_palignr family: palignr, the byte alignment of two xmm registers.
   The source s goes below the destination d as 32 bytes (s in bytes 0..15, d in 16..31); the
   result is the 16 bytes of that starting at byte n. n is imm8 & 0xFF; n of 32 and above gives
   all zeros. */
#ifndef TF_INSNS_SSE_PALIGNR_H
#define TF_INSNS_SSE_PALIGNR_H

#include "../runtime.h"

/* palignr: t[0..15] = s.b[0..15], t[16..31] = d.b[0..15];
   r.b[k] = (k + n < 32) ? t[k + n] : 0 */
TF_INLINE Xmm x_palignr(Xmm d, Xmm s, uint32_t n) {
    Xmm r;
    r.q[0] = 0;
    r.q[1] = 0;
    for (uint32_t k = 0; k < 16 && k + n < 32; k++) {
        uint32_t j = k + n;
        r.b[k] = (j < 16) ? s.b[j] : d.b[j - 16];
    }
    return r;
}

#endif
