"""SSE2 packed right shifts: psrlw, psrld, psrlq (66 0F D1, D2, D3 /r, and the immediate forms
66 0F 71, 72, 73 /2 ib).

Every lane shifts right with zeros coming in; a count above the lane width (15, 31, 63) gives all
zeros. The /r form takes the count from the low 64 bits of an xmm register or of a 16-byte memory
operand; the immediate form takes imm8. The lane loops are C in harness/insns/sse_psrl.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_psrl.h")


def _psrl(tr, helper):
    """Both forms of one mnemonic: xmm, imm8 (66 0F 7x /2 ib) and xmm, xmm/m128 (66 0F Dx /r), whose
    count is the low qword of the source. The form is told by operand 1, not by the ModRM /digit."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not tr.is_mem(1) and not tr.is_reg(1):
        n = tr.ops[1].imm & 0xFF                    # the immediate form
        return ["Xmm s = ctx->xmm[%d];" % d,        # read the source before writing anything
                "ctx->xmm[%d] = %s(s, %d);" % (d, helper, n)]
    if tr.xr(1) is None and not tr.is_mem(1):
        raise Unsupported(tr.name + " source")
    return ["Xmm s = %s;" % tr.xval(1),             # read the count before writing the destination
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s.q[0]);" % (d, helper, d)]


@F.insn("psrlw", enc(0x0FD1, mand=0x66) | enc(0x0F71, reg=2, mand=0x66), vector=True)
def t_psrlw(tr, flow):
    return _psrl(tr, "x_psrlw")


@F.insn("psrld", enc(0x0FD2, mand=0x66) | enc(0x0F72, reg=2, mand=0x66), vector=True)
def t_psrld(tr, flow):
    return _psrl(tr, "x_psrld")


@F.insn("psrlq", enc(0x0FD3, mand=0x66) | enc(0x0F73, reg=2, mand=0x66), vector=True)
def t_psrlq(tr, flow):
    return _psrl(tr, "x_psrlq")
