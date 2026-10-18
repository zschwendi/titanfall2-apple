"""SSE2 high-half unpacks: punpckhbw, punpckhwd, punpckhdq and punpckhqdq (66 0F 68, 69, 6A, 6D /r).

Only the xmm forms are translated: the destination's high half is interleaved with the source's,
destination first. The MMX forms (no 66 prefix) are not registered, so the core refuses them.
The lane loops are C in harness/insns/sse_punpckh.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_punpckh.h")


def _xmm_xmm(tr):
    """Destination xmm number and the source as a C Xmm expression; refuses any other form."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
        raise Unsupported(tr.name + " source")
    return d, tr.xval(1)


@F.insn("punpckhbw", enc(0x0F68, mand=0x66), vector=True)
def t_punpckhbw(tr, flow):
    d, src = _xmm_xmm(tr)
    return ["Xmm s = %s;" % src,                       # read the source before writing anything
            "ctx->xmm[%d] = x_punpckhbw(ctx->xmm[%d], s);" % (d, d)]


@F.insn("punpckhwd", enc(0x0F69, mand=0x66), vector=True)
def t_punpckhwd(tr, flow):
    d, src = _xmm_xmm(tr)
    return ["Xmm s = %s;" % src,
            "ctx->xmm[%d] = x_punpckhwd(ctx->xmm[%d], s);" % (d, d)]


@F.insn("punpckhdq", enc(0x0F6A, mand=0x66), vector=True)
def t_punpckhdq(tr, flow):
    d, src = _xmm_xmm(tr)
    return ["Xmm s = %s;" % src,
            "ctx->xmm[%d] = x_punpckhdq(ctx->xmm[%d], s);" % (d, d)]


@F.insn("punpckhqdq", enc(0x0F6D, mand=0x66), vector=True)
def t_punpckhqdq(tr, flow):
    d, src = _xmm_xmm(tr)
    return ["Xmm s = %s;" % src,
            "ctx->xmm[%d] = x_punpckhqdq(ctx->xmm[%d], s);" % (d, d)]
