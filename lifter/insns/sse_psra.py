"""SSE2 arithmetic right shifts: psraw and psrad, with the encodings 66 0F E1 /r, 66 0F 71 /4 ib,
66 0F E2 /r and 66 0F 72 /4 ib.

The count is an immediate byte (the /4 ib forms; imm8 & 0xFF) or the low 64 bits of an xmm register
or of 16 bytes of memory (the /r forms). A count above 15 (words) or 31 (doublewords) fills every
lane with its sign bit. The lane loops are C in harness/insns/sse_psra.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_psra.h")


def _shift(tr, helper):
    """C lines of psraw or psrad: helper is the C function, its second argument the count.
    Refuses every operand form that is not an xmm, imm8 form or an xmm, xmm/m128 form."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is not None or tr.is_mem(1):             # /r: the count is the low 64 bits of op 1
        if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
            raise Unsupported(tr.name + " count operand")
        return ["Xmm s = %s;" % tr.xval(1),              # read the count before the destination
                "ctx->xmm[%d] = %s(ctx->xmm[%d], s.q[0]);" % (d, helper, d)]
    if tr.is_reg(1):                                     # /4 ib: op 1 must be an immediate
        raise Unsupported(tr.name + " operand")
    return ["Xmm s = ctx->xmm[%d];" % d,
            "ctx->xmm[%d] = %s(s, %d);" % (d, helper, tr.ops[1].imm & 0xFF)]


@F.insn("psraw", enc(0x0FE1, mand=0x66) | enc(0x0F71, reg=4, mand=0x66), vector=True)
def t_psraw(tr, flow):
    return _shift(tr, "x_psraw")


@F.insn("psrad", enc(0x0FE2, mand=0x66) | enc(0x0F72, reg=4, mand=0x66), vector=True)
def t_psrad(tr, flow):
    return _shift(tr, "x_psrad")
