"""SSE2 and SSSE3 packed multiplies: pmuludq (66 0F F4 /r) and pmaddubsw (66 0F 38 04 /r).

The source is an xmm register or 16 bytes of memory; the result replaces the xmm destination.
The lane loops are C in harness/insns/sse_pmuldq.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pmuldq.h")


def _xmm_xmm_or_m128(tr):
    """Destination xmm number; refuses any form whose source is not an xmm register or m128."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
        raise Unsupported(tr.name + " source")
    return d


@F.insn("pmuludq", enc(0x0FF4, mand=0x66), vector=True)
def t_pmuludq(tr, flow):
    d = _xmm_xmm_or_m128(tr)
    return ["Xmm s = %s;" % tr.xval(1),              # read the source before writing anything
            "ctx->xmm[%d] = x_pmuludq(ctx->xmm[%d], s);" % (d, d)]


@F.insn("pmaddubsw", enc(0x0F3804, mand=0x66), vector=True)
def t_pmaddubsw(tr, flow):
    d = _xmm_xmm_or_m128(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_pmaddubsw(ctx->xmm[%d], s);" % (d, d)]
