"""SSE2 packed shifts left: psllw, pslld, psllq (66 0F F1/F2/F3 /r and 66 0F 71/72/73 /6 ib).

Every lane is shifted left by the same count. The count is an immediate byte (the /6 ib form)
or the low 64 bits of an xmm register or of the 16 bytes at a memory operand (the /r form); a
count of the lane width or more gives all zeros. The lane loops are C in harness/insns/sse_psll.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_psll.h")


def _shift(tr, lane_fn):
    """Statements of one shift: destination xmm, count from an immediate or from an xmm/m128."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.is_mem(1) or tr.xr(1) is not None:
        if tr.is_mem(1) and tr.bits(1) != 128:
            raise Unsupported(tr.name + " count operand")
        return ["Xmm c = %s;" % tr.xval(1),              # read the count before writing anything
                "Xmm s = ctx->xmm[%d];" % d,
                "ctx->xmm[%d] = %s(s, c.q[0]);" % (d, lane_fn)]
    if tr.is_reg(1):
        raise Unsupported(tr.name + " count register")
    return ["Xmm s = ctx->xmm[%d];" % d,
            "ctx->xmm[%d] = %s(s, %d);" % (d, lane_fn, tr.ops[1].imm & 0xFF)]


@F.insn("psllw", enc(0x0FF1, mand=0x66) | enc(0x0F71, reg=6, mand=0x66), vector=True)
def t_psllw(tr, flow):
    return _shift(tr, "x_psllw")


@F.insn("pslld", enc(0x0FF2, mand=0x66) | enc(0x0F72, reg=6, mand=0x66), vector=True)
def t_pslld(tr, flow):
    return _shift(tr, "x_pslld")


@F.insn("psllq", enc(0x0FF3, mand=0x66) | enc(0x0F73, reg=6, mand=0x66), vector=True)
def t_psllq(tr, flow):
    return _shift(tr, "x_psllq")
