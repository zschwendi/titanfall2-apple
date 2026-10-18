"""SSE2 saturating subtractions: psubsb, psubsw, psubusb and psubusw (66 0F E8, E9, D8, D9 /r).

Lane by lane, d - s computed in int and clamped to the lane's range: signed lanes for psubsb and
psubsw, unsigned lanes for psubusb and psubusw. The source is an xmm register or 16 bytes of
memory. The lane loops are C in harness/insns/sse_psubs.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_psubs.h")


def _xmm_pair(tr):
    """Destination xmm number; refuses any form whose source is neither an xmm register nor memory."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not tr.is_mem(1):
        raise Unsupported(tr.name + " source")
    return d


@F.insn("psubsb", enc(0x0FE8, mand=0x66), vector=True)
def t_psubsb(tr, flow):
    d = _xmm_pair(tr)
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = x_psubsb(ctx->xmm[%d], s);" % (d, d)]


@F.insn("psubsw", enc(0x0FE9, mand=0x66), vector=True)
def t_psubsw(tr, flow):
    d = _xmm_pair(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_psubsw(ctx->xmm[%d], s);" % (d, d)]


@F.insn("psubusb", enc(0x0FD8, mand=0x66), vector=True)
def t_psubusb(tr, flow):
    d = _xmm_pair(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_psubusb(ctx->xmm[%d], s);" % (d, d)]


@F.insn("psubusw", enc(0x0FD9, mand=0x66), vector=True)
def t_psubusw(tr, flow):
    d = _xmm_pair(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_psubusw(ctx->xmm[%d], s);" % (d, d)]
