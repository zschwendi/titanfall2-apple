"""SSE scalar compares that set EFLAGS: comiss, ucomiss, comisd, ucomisd.

Only the flags change.  ZF, PF, CF are the result of comparing lane 0 (unordered 1 1 1, greater
0 0 0, less 0 0 1, equal 1 0 0) and OF, SF, AF are cleared.  The comi* and ucomi* pairs differ only
in which NaNs signal the invalid exception, which these translations do not write; the flags are
the same for both.

The formulation is the one docs/findings/11-float-mode.md measured exact in the guest mode (its row
"comiss ucomiss comisd ucomisd"): un = an operand is a NaN, ZF = un | a==b, PF = un, CF = un | a<b.
Plain C on the lanes, so no header.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("comiss", enc(0x0F2F, mand=0), vector=True, fp="f32 scalar")
@F.insn("ucomiss", enc(0x0F2E, mand=0), vector=True, fp="f32 scalar")
@F.insn("comisd", enc(0x0F2F, mand=0x66), vector=True, fp="f64 scalar")
@F.insn("ucomisd", enc(0x0F2E, mand=0x66), vector=True, fp="f64 scalar")
def t_comi(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " first operand is not an xmm register")
    ctype, lane, bits = ("float", "f", 32) if tr.name.endswith("ss") else ("double", "pd", 64)
    # comisd's memory operand is 8 bytes whatever the decoder calls it (translate.DECODER_M64);
    # fval reads exactly the 4 or 8 bytes of the lane.
    src = ["%s b = %s;" % (ctype, tr.fval(1, bits))]
    return (["%s a = ctx->xmm[%d].%s[0];" % (ctype, d, lane)]      # both sources are read before any flag is written
            + src +
            ["uint8_t un = (a != a) | (b != b);",
             "ctx->zf = un | (a == b);",
             "ctx->pf = un;",
             "ctx->cf = un | (a < b);",
             "ctx->of = 0;",
             "ctx->sf = 0;",
             "ctx->af = 0;"])
