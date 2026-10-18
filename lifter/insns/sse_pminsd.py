"""SSE4.1 packed minimum of signed doublewords: pminsd (66 0F 38 39 /r).

The source is an xmm register or 16 bytes of memory (aligned: tr.xval emits the fault). The lane
loop is C in harness/insns/sse_pminsd.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pminsd.h")


@F.insn("pminsd", enc(0x0F3839, mand=0x66), vector=True)
def t_pminsd(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported("pminsd destination")
    if tr.is_reg(1) and tr.xr(1) is None:
        raise Unsupported("pminsd source")
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = x_pminsd(ctx->xmm[%d], s);" % (d, d)]
