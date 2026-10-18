"""SSE2 shuffles of the words or doublewords of an xmm register by an immediate: pshufd, pshuflw
and pshufhw (66 0F 70, F2 0F 70 and F3 0F 70 /r ib).

The forms are an xmm destination, an xmm or 16-byte memory source and an immediate byte. The
lane selection is C in harness/insns/sse_pshufd.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pshufd.h")


def _xmm_src_imm(tr):
    """Destination xmm number and control byte of the xmm, xmm/m128, imm8 form; refuses any
    other form."""
    tr.need_ops(3)
    d = tr.xr(0)
    if d is None or tr.is_reg(2) or tr.is_mem(2):
        raise Unsupported(tr.name + " operand")
    if tr.xr(1) is None and not tr.is_mem(1):
        raise Unsupported(tr.name + " source")
    return d, tr.ops[2].imm & 0xFF


@F.insn("pshufd", enc(0x0F70, mand=0x66), vector=True)
def t_pshufd(tr, flow):
    d, imm = _xmm_src_imm(tr)
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = x_pshufd(s, %d);" % (d, imm)]


@F.insn("pshuflw", enc(0x0F70, mand=0xF2), vector=True)
def t_pshuflw(tr, flow):
    d, imm = _xmm_src_imm(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_pshuflw(s, %d);" % (d, imm)]


@F.insn("pshufhw", enc(0x0F70, mand=0xF3), vector=True)
def t_pshufhw(tr, flow):
    d, imm = _xmm_src_imm(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_pshufhw(s, %d);" % (d, imm)]
