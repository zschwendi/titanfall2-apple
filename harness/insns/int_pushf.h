/* C helpers of the int_pushf family: pushfq and popfq.  RFLAGS as lifted code sees it: the
   seven flags of Ctx, bit 1, and IF = 1 (user mode); see lifter/insns/int_pushf.py. */
#ifndef TF_INSNS_INT_PUSHF_H
#define TF_INSNS_INT_PUSHF_H

#include "../runtime.h"

/* TF (8), NT (14), AC (18), ID (21): a popfq that sets one of them traps. */
#define TF_INT_PUSHF_TRAP UINT64_C(0x244100)

TF_INLINE uint64_t x_pushfq_value(const Ctx *ctx) {
    return (uint64_t)ctx->cf | 2u | (uint64_t)ctx->pf << 2 | (uint64_t)ctx->af << 4 | (uint64_t)ctx->zf << 6
           | (uint64_t)ctx->sf << 7 | 0x200u | (uint64_t)ctx->df << 10 | (uint64_t)ctx->of << 11;
}

TF_INLINE void x_popfq_load(Ctx *ctx, uint64_t v) {
    ctx->cf = v & 1;
    ctx->pf = (v >> 2) & 1;
    ctx->af = (v >> 4) & 1;
    ctx->zf = (v >> 6) & 1;
    ctx->sf = (v >> 7) & 1;
    ctx->df = (v >> 10) & 1;
    ctx->of = (v >> 11) & 1;
}

#endif
