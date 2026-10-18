"""SSE scalar float arithmetic: addss, addsd.  The pattern for every float family.

A float instruction is one whose result depends on rounding, NaN rules or MXCSR.  It is declared
with fp=...: the lane type and scalar or packed, nothing else.  The harness does the rest
(harness/floatcase.py): float-aware operand values, every MXCSR variant, the real CPU as the
expected side.

The translation is the one docs/findings/11-float-mode.md measured exact in the guest float
mode (harness/fpmode.h): an IEEE addition on lane 0, upper lanes untouched.  Addition is
commutative, so it goes through tf_fadd32/tf_fadd64 of harness/runtime.h, which keep the operand
order: with two NaN operands x86 returns the first one (the destination), quieted.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("addss", enc(0x0F58, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("addsd", enc(0x0F58, mand=0xF2), vector=True, fp="f64 scalar")
def t_add_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    bits, lane, ctype = (32, "f", "float") if tr.name == "addss" else (64, "pd", "double")
    return ["%s s = %s;" % (ctype, tr.fval(1, bits)),              # the source is read before anything is written
            "ctx->xmm[%d].%s[0] = tf_fadd%d(ctx->xmm[%d].%s[0], s);" % (d, lane, bits, d, lane)]
