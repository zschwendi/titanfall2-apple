"""SSE2 packed integer adds: paddb, paddw, paddd and paddq (66 0F FC, FD, FE and D4 /r).

Wrapping lane-wise addition of the whole xmm register: the source is an xmm register or 16 bytes
of memory, and the result replaces the destination. The lane loops are C in
harness/insns/sse_padd.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_padd.h")


def _padd(tr, helper):
    """Destination xmm number; the source is read whole (an xmm register or an m128) before the
    destination is written. Any other form is refused by tr.xval or here."""
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    return ["Xmm s = %s;" % tr.xval(1),            # read the source before writing anything
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, helper, d)]


@F.insn("paddb", enc(0x0FFC, mand=0x66), vector=True)
def t_paddb(tr, flow):
    return _padd(tr, "x_paddb")


@F.insn("paddw", enc(0x0FFD, mand=0x66), vector=True)
def t_paddw(tr, flow):
    return _padd(tr, "x_paddw")


@F.insn("paddd", enc(0x0FFE, mand=0x66), vector=True)
def t_paddd(tr, flow):
    return _padd(tr, "x_paddd")


@F.insn("paddq", enc(0x0FD4, mand=0x66), vector=True)
def t_paddq(tr, flow):
    return _padd(tr, "x_paddq")
