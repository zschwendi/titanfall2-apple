"""SSE2 pack instructions with saturation: packuswb, packsswb and packssdw (66 0F 67, 66 0F 63,
66 0F 6B), in the xmm,xmm and xmm,m128 forms. The lane work is C in harness/insns/sse_pack.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pack.h")


def _operands(tr):
    """Destination xmm number of the xmm, xmm/m128 form; refuses any other form."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or (tr.xr(1) is None and not tr.is_mem(1)):
        raise Unsupported(tr.name + " operand")
    return d


@F.insn("packuswb", enc(0x0F67, mand=0x66), vector=True)
def t_packuswb(tr, flow):
    d = _operands(tr)
    return ["Xmm s = %s;" % tr.xval(1),                 # read the source before writing anything
            "ctx->xmm[%d] = x_packuswb(ctx->xmm[%d], s);" % (d, d)]


@F.insn("packsswb", enc(0x0F63, mand=0x66), vector=True)
def t_packsswb(tr, flow):
    d = _operands(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_packsswb(ctx->xmm[%d], s);" % (d, d)]


@F.insn("packssdw", enc(0x0F6B, mand=0x66), vector=True)
def t_packssdw(tr, flow):
    d = _operands(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = x_packssdw(ctx->xmm[%d], s);" % (d, d)]
