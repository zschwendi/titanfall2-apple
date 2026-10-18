"""16-byte xmm moves to and from memory: lddqu (F2 0F F0, SSE3), movntps (0F 2B) and movntdq
(66 0F E7, SSE2). No register-to-register form exists; the non-temporal hint is not observable.

lddqu has no alignment requirement, so its memory operand is read with tr.xval as well; the
movnt stores are aligned operands and go through tr.xstore, which emits the #GP check.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("lddqu", enc(0x0FF0, mand=0xF2), vector=True)
def t_lddqu(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or tr.is_reg(1) or not tr.is_mem(1):
        raise Unsupported("lddqu operand")
    return ["Xmm s = %s;" % tr.xval(1),                # load first, then write the destination
            "ctx->xmm[%d] = s;" % d]


def _movnt(tr):
    """Source xmm number of the xmm-to-memory store form; refuses any other form."""
    tr.need_ops(2)
    s = tr.xr(1)
    if s is None or not tr.is_mem(0):
        raise Unsupported(tr.name + " operand")
    return s


@F.insn("movntps", enc(0x0F2B, mand=0), vector=True)
def t_movntps(tr, flow):
    s = _movnt(tr)
    return ["Xmm v = ctx->xmm[%d];" % s,
            tr.xstore(0, "v")]


@F.insn("movntdq", enc(0x0FE7, mand=0x66), vector=True)
def t_movntdq(tr, flow):
    s = _movnt(tr)
    return ["Xmm v = ctx->xmm[%d];" % s,
            tr.xstore(0, "v")]
