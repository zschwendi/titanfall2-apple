"""SSE2 packed integer subtraction: psubb, psubw, psubd, psubq (66 0F F8..FB /r).

Each lane of the destination xmm register minus the same lane of the source (an xmm register or
16 bytes of memory), wrapping modulo the lane size. The lane loops are C in harness/insns/sse_psub.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_psub.h")


def _psub(tr, fn):
    """Statements of a packed subtraction with the C helper fn; refuses any form but xmm, xmm and
    xmm, m128."""
    tr.need_ops(2)
    d = tr.xr(0)
    s = tr.xr(1)
    if d is None or (s is None and not (tr.is_mem(1) and tr.bits(1) == 128)):
        raise Unsupported(tr.name + " operand")
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, fn, d)]


@F.insn("psubb", enc(0x0FF8, mand=0x66), vector=True)
def t_psubb(tr, flow):
    return _psub(tr, "x_psubb")


@F.insn("psubw", enc(0x0FF9, mand=0x66), vector=True)
def t_psubw(tr, flow):
    return _psub(tr, "x_psubw")


@F.insn("psubd", enc(0x0FFA, mand=0x66), vector=True)
def t_psubd(tr, flow):
    return _psub(tr, "x_psubd")


@F.insn("psubq", enc(0x0FFB, mand=0x66), vector=True)
def t_psubq(tr, flow):
    return _psub(tr, "x_psubq")
