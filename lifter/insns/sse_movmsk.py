"""SSE sign-bit gathers: movmskps, movmskpd (0F 50, 66 0F 50) and pmovmskb (66 0F D7).

Each puts one bit per lane of an xmm register, the lane's sign bit, into a general register:
bit k of the result comes from lane k, and all higher bits are zero. The only form is a 32-bit
general register and an xmm register; anything else is refused. The lane loops are C in
harness/insns/sse_movmsk.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_movmsk.h")


def _gpr_xmm(tr):
    """Source xmm number of the r32, xmm form; refuses any other form."""
    tr.need_ops(2)
    if tr.xr(0) is not None or not tr.is_reg(0) or tr.bits(0) != 32:
        raise Unsupported(tr.name + " destination")
    s = tr.xr(1)
    if s is None:
        raise Unsupported(tr.name + " source")
    return s


@F.insn("movmskps", enc(0x0F50, mand=0), vector=True)
def t_movmskps(tr, flow):
    s = _gpr_xmm(tr)
    return ["Xmm s = ctx->xmm[%d];" % s,             # read the source before writing anything
            tr.wr(0, "x_movmskps(s)", 32)]


@F.insn("movmskpd", enc(0x0F50, mand=0x66), vector=True)
def t_movmskpd(tr, flow):
    s = _gpr_xmm(tr)
    return ["Xmm s = ctx->xmm[%d];" % s,
            tr.wr(0, "x_movmskpd(s)", 32)]


@F.insn("pmovmskb", enc(0x0FD7, mand=0x66), vector=True)
def t_pmovmskb(tr, flow):
    s = _gpr_xmm(tr)
    return ["Xmm s = ctx->xmm[%d];" % s,
            tr.wr(0, "x_pmovmskb(s)", 32)]
