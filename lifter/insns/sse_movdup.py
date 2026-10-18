"""SSE3 duplicating moves: movddup, movshdup, movsldup.

movddup xmm, xmm/m64 (F2 0F 12 /r), movshdup xmm, xmm/m128 (F3 0F 16 /r) and movsldup xmm,
xmm/m128 (F3 0F 12 /r). The lane copies are C in harness/insns/sse_movdup.h. The movddup memory
operand is 8 bytes and has no alignment check; the m128 operands of movshdup and movsldup are read
with tr.xval, which faults when they are not 16-byte aligned, as the CPU does.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_movdup.h")


def _dest(tr):
    """Destination xmm number; refuses unless operand 1 is an xmm register or a memory operand."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or (tr.xr(1) is None and not tr.is_mem(1)):
        raise Unsupported(tr.name + " operand")
    return d


@F.insn("movddup", enc(0x0F12, mand=0xF2), vector=True)
def t_movddup(tr, flow):
    d = _dest(tr)
    s = tr.xr(1)
    if s is None:                                       # m64: 8 bytes, no alignment check
        return ["ctx->xmm[%d] = x_movddup(%s(%s));" % (d, tr.mac(1, "LD64"), tr.ea(1))]
    return ["uint64_t v = ctx->xmm[%d].q[0];" % s,      # read the source before writing anything
            "ctx->xmm[%d] = x_movddup(v);" % d]


@F.insn("movshdup", enc(0x0F16, mand=0xF3), vector=True)
def t_movshdup(tr, flow):
    d = _dest(tr)
    return ["Xmm s = %s;" % tr.xval(1),                 # read the source before writing anything
            "ctx->xmm[%d] = x_movshdup(s);" % d]


@F.insn("movsldup", enc(0x0F12, mand=0xF3), vector=True)
def t_movsldup(tr, flow):
    d = _dest(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_movsldup(s);" % d]
