"""SSE2 whole-register byte shifts: psrldq and pslldq (66 0F 73 /3 ib and 66 0F 73 /7 ib).

The only form is an xmm register and an immediate byte count n; n above 15 gives zero. The lane
loops are C in harness/insns/sse_shift_dq.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_shift_dq.h")


def _xmm_imm(tr):
    """Destination xmm number and byte count of the xmm, imm8 form; refuses any other form."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or tr.is_reg(1) or tr.is_mem(1):
        raise Unsupported(tr.name + " operand")
    return d, tr.ops[1].imm & 0xFF


@F.insn("psrldq", enc(0x0F73, reg=3, mand=0x66), vector=True)
def t_psrldq(tr, flow):
    d, n = _xmm_imm(tr)
    return ["Xmm s = ctx->xmm[%d];" % d,             # read the source before writing anything
            "ctx->xmm[%d] = x_psrldq(s, %d);" % (d, n)]


@F.insn("pslldq", enc(0x0F73, reg=7, mand=0x66), vector=True)
def t_pslldq(tr, flow):
    d, n = _xmm_imm(tr)
    return ["Xmm s = ctx->xmm[%d];" % d,
            "ctx->xmm[%d] = x_pslldq(s, %d);" % (d, n)]
