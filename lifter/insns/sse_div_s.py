"""SSE scalar float division: divss, divsd.

A float instruction is one whose result depends on rounding, NaN rules or MXCSR, so it is declared
with fp=...: the lane type and scalar or packed, nothing else.  The harness does the rest
(harness/floatcase.py): float-aware operand values, every MXCSR variant, the real CPU as the
expected side.

The translation is the one docs/findings/11-float-mode.md measured exact in the guest float mode
(harness/fpmode.h), row "addss/sd/ps/pd, sub, mul, div": plain C division on lane 0, upper lanes
untouched.  Division is not commutative, so the operand order is the x86 one (destination divided
by source) and needs no helper.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("divss", enc(0x0F5E, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("divsd", enc(0x0F5E, mand=0xF2), vector=True, fp="f64 scalar")
def t_div_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    bits, lane, ctype = (32, "f", "float") if tr.name == "divss" else (64, "pd", "double")
    return ["%s s = %s;" % (ctype, tr.fval(1, bits)),              # the source is read before anything is written
            "ctx->xmm[%d].%s[0] = ctx->xmm[%d].%s[0] / s;" % (d, lane, d, lane)]
