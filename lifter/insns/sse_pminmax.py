"""SSE2 packed integer minimum and maximum: pmaxsw, pminsw (signed words) and pmaxub, pminub
(unsigned bytes); 66 0F EE, 66 0F EA, 66 0F DE and 66 0F DA, xmm register or 16-byte memory source.

The lane loops are C in harness/insns/sse_pminmax.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pminmax.h")


def _xmm_xmm(tr, helper):
    """Statements of an xmm, xmm/m128 instruction: helper(destination, source) into the xmm register.
    Refuses any other operand form."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
        raise Unsupported(tr.name + " source")
    return ["Xmm s = %s;" % tr.xval(1),            # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, helper, d)]


@F.insn("pmaxsw", enc(0x0FEE, mand=0x66), vector=True)
def t_pmaxsw(tr, flow):
    return _xmm_xmm(tr, "x_pmaxsw")


@F.insn("pminsw", enc(0x0FEA, mand=0x66), vector=True)
def t_pminsw(tr, flow):
    return _xmm_xmm(tr, "x_pminsw")


@F.insn("pmaxub", enc(0x0FDE, mand=0x66), vector=True)
def t_pmaxub(tr, flow):
    return _xmm_xmm(tr, "x_pmaxub")


@F.insn("pminub", enc(0x0FDA, mand=0x66), vector=True)
def t_pminub(tr, flow):
    return _xmm_xmm(tr, "x_pminub")
