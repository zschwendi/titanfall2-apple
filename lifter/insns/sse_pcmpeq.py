"""SSE2 packed integer compares: pcmpeqb, pcmpeqw and pcmpeqd (66 0F 74, 75, 76 /r).

Each lane of the destination becomes all ones where it equals the source lane, else all zeros.
The source is an xmm register or 16 bytes of memory (tr.xval aligns and faults as the CPU does).
The lane loops are C in harness/insns/sse_pcmpeq.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pcmpeq.h")


def _dest(tr):
    """Destination xmm number; refuses a source that is a general register."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None or (tr.is_reg(1) and tr.xr(1) is None):
        raise Unsupported(tr.name + " operand")
    return d


def _pcmpeq(tr, helper):
    d = _dest(tr)
    return ["Xmm s = %s;" % tr.xval(1),              # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, helper, d)]


@F.insn("pcmpeqb", enc(0x0F74, mand=0x66), vector=True)
def t_pcmpeqb(tr, flow):
    return _pcmpeq(tr, "x_pcmpeqb")


@F.insn("pcmpeqw", enc(0x0F75, mand=0x66), vector=True)
def t_pcmpeqw(tr, flow):
    return _pcmpeq(tr, "x_pcmpeqw")


@F.insn("pcmpeqd", enc(0x0F76, mand=0x66), vector=True)
def t_pcmpeqd(tr, flow):
    return _pcmpeq(tr, "x_pcmpeqd")
