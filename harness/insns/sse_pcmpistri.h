/* C helper of the sse_pcmpistri family: pcmpistri xmm1, xmm2/m128, imm8 (Intel SDM vol. 2,
   section 4.1 "Imm8 control byte operation for PCMPESTRI / PCMPESTRM / PCMPISTRI / PCMPISTRM",
   and the PCMPISTRI entry).

   a is xmm1, b is xmm2/m128.  Each is a string of n elements that ends before its first zero
   element (the implicit length); elements from there on are invalid.  The four steps:

     1 source data format, imm8[1:0]   00 = 16 unsigned bytes, 01 = 8 unsigned words
                                        (10 and 11, signed, are not translated)
     2 aggregation, imm8[3:2]          IntRes1, one bit per element j of b:
                                        00 equal any      b[j] equals some a[i]
                                        01 ranges         a[i] <= b[j] <= a[i+1] for some even i
                                        11 equal ordered  a occurs in b at j (a substring search)
                                        (10, equal each, is not translated)
     3 polarity, imm8[5:4]             00 IntRes2 = IntRes1, 01 IntRes2 = ~IntRes1 (n bits)
                                        (10 and 11, masked, are not translated)
     4 output selection, imm8[6]       ECX = index of the least (0) or most (1) significant set
                                        bit of IntRes2, or n if IntRes2 is zero

   Flags: CF = IntRes2 != 0, ZF = b has a zero element, SF = a has a zero element,
   OF = IntRes2[0], AF = PF = 0.  ECX is written as a 32-bit register: bits 63:32 of RCX are 0.

   The translator passes only control bytes it has tested (lifter/insns/sse_pcmpistri.py,
   CONTROL); this function is not defined for any other. */
#ifndef TF_INSNS_SSE_PCMPISTRI_H
#define TF_INSNS_SSE_PCMPISTRI_H

#include "../runtime.h"

TF_INLINE void x_pcmpistri(Ctx *ctx, Xmm a, Xmm b, uint32_t ctl) {
    /* 1: the elements, widened to 16 bits, and the two implicit lengths */
    uint32_t n = (ctl & 1) ? 8 : 16;
    uint16_t ea[16], eb[16];
    uint32_t la = n, lb = n;
    for (uint32_t k = n; k-- > 0;) {
        ea[k] = (ctl & 1) ? a.w[k] : a.b[k];
        eb[k] = (ctl & 1) ? b.w[k] : b.b[k];
        if (ea[k] == 0) la = k;
        if (eb[k] == 0) lb = k;
    }
    /* 2: aggregation.  A pair with an invalid element compares as the SDM's table 4-7 says:
       equal any and ranges: false; equal ordered: true if a[i] is invalid, else false. */
    uint32_t r1 = 0;
    switch ((ctl >> 2) & 3) {
    case 0:
        for (uint32_t j = 0; j < lb; j++)
            for (uint32_t i = 0; i < la; i++)
                if (eb[j] == ea[i]) r1 |= 1u << j;
        break;
    case 1:
        for (uint32_t j = 0; j < lb; j++)
            for (uint32_t i = 0; i + 1 < la; i += 2)
                if (ea[i] <= eb[j] && eb[j] <= ea[i + 1]) r1 |= 1u << j;
        break;
    default:
        for (uint32_t j = 0; j < n; j++) {
            uint32_t m = 1;
            for (uint32_t i = 0; i < n - j && i < la; i++)      /* i >= la: forced true */
                if (j + i >= lb || ea[i] != eb[j + i]) { m = 0; break; }
            r1 |= m << j;
        }
        break;
    }
    /* 3: polarity */
    uint32_t r2 = (ctl & 0x10) ? ~r1 & ((1u << n) - 1) : r1;
    /* 4: the index */
    uint32_t idx = n;
    if (r2 != 0) {
        if (ctl & 0x40) { idx = n - 1; while (!((r2 >> idx) & 1)) idx--; }
        else            { idx = 0;     while (!((r2 >> idx) & 1)) idx++; }
    }
    ctx->r[1] = idx;
    ctx->cf = r2 != 0;
    ctx->zf = lb < n;
    ctx->sf = la < n;
    ctx->of = r2 & 1;
    ctx->af = 0;
    ctx->pf = 0;
}

#endif
