"""SSE2 interleaves of the low halves: punpcklbw, punpcklwd, punpckldq and punpcklqdq
(66 0F 60, 61, 62 and 6C /r).

The destination is an xmm register; the source is an xmm register or 16 bytes of memory. The
MMX forms (no 66 prefix) are not registered, so the core refuses them. The lane loops are C in
harness/insns/sse_punpckl.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_punpckl.h")


def _xmm_xmm(tr):
    """Destination xmm number; refuses any source that is neither an xmm register nor 16 bytes of
    memory."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or (tr.is_reg(1) and tr.xr(1) is None):
        raise Unsupported(tr.name + " operand")
    if tr.is_mem(1) and tr.bits(1) != 128:
        raise Unsupported(tr.name + " memory operand")
    return d


@F.insn("punpcklbw", enc(0x0F60, mand=0x66), vector=True)
def t_punpcklbw(tr, flow):
    d = _xmm_xmm(tr)
    return ["Xmm s = %s;" % tr.xval(1),              # read the source before writing anything
            "ctx->xmm[%d] = x_punpcklbw(ctx->xmm[%d], s);" % (d, d)]


@F.insn("punpcklwd", enc(0x0F61, mand=0x66), vector=True)
def t_punpcklwd(tr, flow):
    d = _xmm_xmm(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_punpcklwd(ctx->xmm[%d], s);" % (d, d)]


@F.insn("punpckldq", enc(0x0F62, mand=0x66), vector=True)
def t_punpckldq(tr, flow):
    d = _xmm_xmm(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_punpckldq(ctx->xmm[%d], s);" % (d, d)]


@F.insn("punpcklqdq", enc(0x0F6C, mand=0x66), vector=True)
def t_punpcklqdq(tr, flow):
    d = _xmm_xmm(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_punpcklqdq(ctx->xmm[%d], s);" % (d, d)]
