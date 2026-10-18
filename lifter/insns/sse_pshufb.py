"""SSSE3 byte shuffle: pshufb (66 0F 38 00 /r).

The destination xmm is the table of bytes and is written; the source (an xmm register or 16 bytes
of memory) holds the control bytes. The lane loop is C in harness/insns/sse_pshufb.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pshufb.h")


@F.insn("pshufb", enc(0x0F3800, mand=0x66), vector=True)
def t_pshufb(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported("pshufb destination")
    return ["Xmm s = %s;" % tr.xval(1),              # the control bytes first: d may be the source
            "ctx->xmm[%d] = x_pshufb(ctx->xmm[%d], s);" % (d, d)]
