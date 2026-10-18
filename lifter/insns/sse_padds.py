"""SSE2 saturating packed adds: paddsb, paddsw, paddusb and paddusw (66 0F EC, ED, DC and DD /r).

Lane by lane on the whole register: the xmm destination, and an xmm register or 16 bytes of
memory as the source. The lane loops are C in harness/insns/sse_padds.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_padds.h")


def _lanes(tr, helper):
    """The statements of an xmm, xmm/m128 saturating add with the given C helper."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
        raise Unsupported(tr.name + " source")
    return ["Xmm s = %s;" % tr.xval(1),              # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, helper, d)]


@F.insn("paddsb", enc(0x0FEC, mand=0x66), vector=True)
def t_paddsb(tr, flow):
    return _lanes(tr, "x_paddsb")


@F.insn("paddsw", enc(0x0FED, mand=0x66), vector=True)
def t_paddsw(tr, flow):
    return _lanes(tr, "x_paddsw")


@F.insn("paddusb", enc(0x0FDC, mand=0x66), vector=True)
def t_paddusb(tr, flow):
    return _lanes(tr, "x_paddusb")


@F.insn("paddusw", enc(0x0FDD, mand=0x66), vector=True)
def t_paddusw(tr, flow):
    return _lanes(tr, "x_paddusw")
