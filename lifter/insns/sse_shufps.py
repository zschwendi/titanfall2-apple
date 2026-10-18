"""SSE shuffles by an immediate: shufps (0F C6 /r ib) and shufpd (66 0F C6 /r ib).

The destination is an xmm register; the source is an xmm register or 16 bytes of memory (aligned:
tr.xval emits the fault). The integer bits are moved and never read as floats, so there is no
float declaration.

The translation is one host vector instruction on whole registers (harness/runtime.h, THE
PACKED FORMS, "Lane shuffles"): the lanes are chosen here, from the immediate, when the code is
translated, and written as literals in TF_SHUF4 / TF_SHUF2:
  shufps: TF_SHUF4(dst, src, n & 3, (n >> 2) & 3, 4 + ((n >> 4) & 3), 4 + ((n >> 6) & 3))
  shufpd: TF_SHUF2(dst, src, n & 1, 2 + ((n >> 1) & 1))
The source is read whole into a local first, before the destination is written (the source may
be the destination register, and a memory source must fault before anything is written).
The x_shufps / x_shufpd helpers in harness/insns/sse_shufps.h are no longer used by this file.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_shufps.h")


def _xmm_xmm_imm(tr):
    """Destination xmm number and immediate byte of the xmm, xmm/m128, imm8 form; refuses any other form."""
    tr.need_ops(3)
    d = tr.xr(0)
    if d is None or (tr.xr(1) is None and not tr.is_mem(1)) or tr.is_reg(2) or tr.is_mem(2):
        raise Unsupported(tr.name + " operand")
    return d, tr.ops[2].imm & 0xFF


@F.insn("shufps", enc(0x0FC6, mand=0), vector=True)
def t_shufps(tr, flow):
    d, n = _xmm_xmm_imm(tr)
    # Lanes 0-1 come from the destination, 2-3 from the source (indices 4-7 are source lanes 0-3).
    i0, i1 = n & 3, (n >> 2) & 3
    i2, i3 = 4 + ((n >> 4) & 3), 4 + ((n >> 6) & 3)
    return ["Xmm s = %s;" % tr.xval(1),              # read the source before writing anything
            "ctx->xmm[%d] = TF_SHUF4(ctx->xmm[%d], s, %d, %d, %d, %d);" % (d, d, i0, i1, i2, i3)]


@F.insn("shufpd", enc(0x0FC6, mand=0x66), vector=True)
def t_shufpd(tr, flow):
    d, n = _xmm_xmm_imm(tr)
    # Lane 0 from the destination, lane 1 from the source (indices 2-3 are source lanes 0-1).
    i0, i1 = n & 1, 2 + ((n >> 1) & 1)
    return ["Xmm s = %s;" % tr.xval(1),
            "ctx->xmm[%d] = TF_SHUF2(ctx->xmm[%d], s, %d, %d);" % (d, d, i0, i1)]
