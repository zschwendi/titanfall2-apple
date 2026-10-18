"""Packed multiplies of 16-bit lanes (pmullw, pmulhw, pmulhuw: 66 0F D5, 66 0F E5, 66 0F E4)
and of 32-bit lanes (pmulld: 66 0F 38 40). Only the xmm forms are translated; the lane loops are
C in harness/insns/sse_pmul.h. The MMX forms have no 66 prefix and never reach these
translators."""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pmul.h")


def _lanes(tr, helper):
    """Destination xmm, source an xmm register or a 16-byte memory operand; the header's lane
    function applied to both. Refuses any other form."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
        raise Unsupported(tr.name + " source")
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, helper, d)]


@F.insn("pmullw", enc(0x0FD5, mand=0x66), vector=True)
def t_pmullw(tr, flow):
    return _lanes(tr, "x_pmullw")


@F.insn("pmulhw", enc(0x0FE5, mand=0x66), vector=True)
def t_pmulhw(tr, flow):
    return _lanes(tr, "x_pmulhw")


@F.insn("pmulhuw", enc(0x0FE4, mand=0x66), vector=True)
def t_pmulhuw(tr, flow):
    return _lanes(tr, "x_pmulhuw")


@F.insn("pmulld", enc(0x0F3840, mand=0x66), vector=True)
def t_pmulld(tr, flow):
    return _lanes(tr, "x_pmulld")
