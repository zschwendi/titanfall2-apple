"""SSE2 packed signed compares: pcmpgtb, pcmpgtw, pcmpgtd (66 0F 64, 66 0F 65, 66 0F 66 /r).

Each lane of the destination becomes all ones where the destination lane is greater than the
source lane as signed numbers, all zeros otherwise. The source is an xmm register or 16 bytes of
memory. The MMX forms have no 66 prefix and are not registered here. The lane loops are C in
harness/insns/sse_pcmpgt.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pcmpgt.h")


def _xmm_dst_xmm_or_mem_src(tr):
    """Destination xmm number; refuses any operand shape other than xmm, xmm|m128."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or (tr.xr(1) is None and not tr.is_mem(1)):
        raise Unsupported(tr.name + " operand")
    return d


def _pcmpgt(tr, helper):
    d = _xmm_dst_xmm_or_mem_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, helper, d)]


@F.insn("pcmpgtb", enc(0x0F64, mand=0x66), vector=True)
def t_pcmpgtb(tr, flow):
    return _pcmpgt(tr, "x_pcmpgtb")


@F.insn("pcmpgtw", enc(0x0F65, mand=0x66), vector=True)
def t_pcmpgtw(tr, flow):
    return _pcmpgt(tr, "x_pcmpgtw")


@F.insn("pcmpgtd", enc(0x0F66, mand=0x66), vector=True)
def t_pcmpgtd(tr, flow):
    return _pcmpgt(tr, "x_pcmpgtd")
