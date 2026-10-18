"""SSE2 rounded averages of unsigned lanes: pavgb and pavgw (66 0F E0 /r and 66 0F E3 /r).

Every lane is (d + s + 1) >> 1 of the two unsigned lanes, computed without overflow. The source
is an xmm register or 16 bytes of memory. The lane loops are C in harness/insns/sse_pavg.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pavg.h")


def _xmm_dst_src(tr):
    """Destination xmm number; the source must be an xmm register or a memory operand (refuses others)."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or (tr.xr(1) is None and not tr.is_mem(1)):
        raise Unsupported(tr.name + " operand")
    return d


@F.insn("pavgb", enc(0x0FE0, mand=0x66), vector=True)
def t_pavgb(tr, flow):
    d = _xmm_dst_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),            # read the source before writing anything
            "ctx->xmm[%d] = x_pavgb(ctx->xmm[%d], s);" % (d, d)]


@F.insn("pavgw", enc(0x0FE3, mand=0x66), vector=True)
def t_pavgw(tr, flow):
    d = _xmm_dst_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_pavgw(ctx->xmm[%d], s);" % (d, d)]
