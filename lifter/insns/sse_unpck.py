"""SSE unpacks: unpcklps, unpckhps (0F 14 /r, 0F 15 /r) and unpcklpd, unpckhpd (66 0F 14 /r,
66 0F 15 /r). They interleave the low or high halves of two xmm registers; bits are moved, not
computed. The source is an xmm register or 16 bytes of memory, read with tr.xval (aligned, as the
CPU requires).

The translation is one host shuffle of whole registers (harness/runtime.h, "Lane shuffles of whole
registers": TF_SHUF4 for the 32-bit lanes, TF_SHUF2 for the 64-bit lanes), with the lanes chosen
here from the immediate-free encoding: unpcklps {d0,s0,d1,s1}, unpckhps {d2,s2,d3,s3},
unpcklpd {d0,s0}, unpckhpd {d1,s1}. The source is read whole into a local before the destination is
written (the source may be the destination register, and a memory source must fault first)."""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_unpck.h")


def _dest_src(tr):
    """Destination xmm number; refuses any operand form but xmm, xmm/m128."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None and not (tr.is_mem(1) and tr.bits(1) == 128):
        raise Unsupported(tr.name + " source")
    return d


@F.insn("unpcklps", enc(0x0F14, mand=0), vector=True)
def t_unpcklps(tr, flow):
    d = _dest_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),              # read the source before writing anything
            "ctx->xmm[%d] = TF_SHUF4(ctx->xmm[%d], s, 0, 4, 1, 5);" % (d, d)]


@F.insn("unpckhps", enc(0x0F15, mand=0), vector=True)
def t_unpckhps(tr, flow):
    d = _dest_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = TF_SHUF4(ctx->xmm[%d], s, 2, 6, 3, 7);" % (d, d)]


@F.insn("unpcklpd", enc(0x0F14, mand=0x66), vector=True)
def t_unpcklpd(tr, flow):
    d = _dest_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = TF_SHUF2(ctx->xmm[%d], s, 0, 2);" % (d, d)]


@F.insn("unpckhpd", enc(0x0F15, mand=0x66), vector=True)
def t_unpckhpd(tr, flow):
    d = _dest_src(tr)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = TF_SHUF2(ctx->xmm[%d], s, 1, 3);" % (d, d)]
