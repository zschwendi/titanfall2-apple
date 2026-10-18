"""SSE3 byte alignment: palignr (66 0F 3A 0F /r ib).

The source goes below the destination as 32 bytes, and the result is the 16 bytes of that starting
at byte imm8 (32 and above give zero). The lane loop is C in harness/insns/sse_palignr.h.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_palignr.h")


@F.insn("palignr", enc(0x0F3A0F, mand=0x66), vector=True)
def t_palignr(tr, flow):
    tr.need_ops(3)
    d = tr.xr(0)
    src_ok = tr.xr(1) is not None or (tr.is_mem(1) and tr.bits(1) == 128)
    if d is None or not src_ok or tr.is_reg(2) or tr.is_mem(2):
        raise Unsupported(tr.name + " operand")
    n = tr.ops[2].imm & 0xFF
    return ["Xmm s = %s;" % tr.xval(1),             # read the source before writing anything
            "ctx->xmm[%d] = x_palignr(ctx->xmm[%d], s, %d);" % (d, d, n)]
