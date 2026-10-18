"""SSE2 sum of absolute differences of bytes: psadbw (66 0F F6 /r).

The destination is an xmm register; the source is an xmm register or 16 bytes of memory. The lane
loop is C in harness/insns/sse_psadbw.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_psadbw.h")


@F.insn("psadbw", enc(0x0FF6, mand=0x66), vector=True)
def t_psadbw(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    s = tr.xr(1)
    if d is None or (s is None and not tr.is_mem(1)):
        raise Unsupported("psadbw operand")
    return ["Xmm s = %s;" % tr.xval(1),               # read the source before writing anything
            "ctx->xmm[%d] = x_psadbw(ctx->xmm[%d], s);" % (d, d)]
