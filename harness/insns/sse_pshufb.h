/* C helpers of the sse_pshufb family: pshufb, a byte shuffle of the destination register under the
   control bytes of the source. */
#ifndef TF_INSNS_SSE_PSHUFB_H
#define TF_INSNS_SSE_PSHUFB_H

#include "../runtime.h"

/* pshufb: r.b[k] = (s.b[k] & 0x80) ? 0 : d.b[s.b[k] & 15]; d is the table, s the control bytes.
   Every byte of r is written. */
TF_INLINE Xmm x_pshufb(Xmm d, Xmm s) {
    Xmm r;
    for (uint32_t k = 0; k < 16; k++)
        r.b[k] = (s.b[k] & 0x80) ? 0 : d.b[s.b[k] & 15];
    return r;
}

#endif
