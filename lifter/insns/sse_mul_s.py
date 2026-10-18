"""SSE scalar float multiplication: mulss, mulsd.  The float pattern of sse_scalar_arith.

mulss multiplies the low single, mulsd the low double; the upper lanes of the destination stay.
Lane 0 is an IEEE multiplication, measured exact in the guest float mode (harness/fpmode.h,
docs/findings/11-float-mode.md).  Multiplication is commutative, so it goes through
tf_fmul32/tf_fmul64 of harness/runtime.h, which keep the operand order: with two NaN operands
x86 returns the first one (the destination), quieted.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("mulss", enc(0x0F59, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("mulsd", enc(0x0F59, mand=0xF2), vector=True, fp="f64 scalar")
def t_mul_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    bits, lane, ctype = (32, "f", "float") if tr.name == "mulss" else (64, "pd", "double")
    return ["%s s = %s;" % (ctype, tr.fval(1, bits)),              # the source is read before anything is written
            "ctx->xmm[%d].%s[0] = tf_fmul%d(ctx->xmm[%d].%s[0], s);" % (d, lane, bits, d, lane)]
