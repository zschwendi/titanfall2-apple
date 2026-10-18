"""SSE scalar conversions between single and double: cvtss2sd, cvtsd2ss.

Both are float instructions (the result depends on rounding and MXCSR), declared with fp=: the
lane type each one reads.  The translation is the one docs/findings/11-float-mode.md measured
exact in the guest float mode (harness/fpmode.h), column "Exact C": a plain C conversion of lane 0.
The conversion rounds by MXCSR.RC, which the guest mode maps (RMode).  Both are scalar: the
destination's other bits stay as they were.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("cvtss2sd", enc(0x0F5A, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cvtsd2ss", enc(0x0F5A, mand=0xF2), vector=True, fp="f64 scalar")
def t_cvt_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.name == "cvtss2sd":
        return ["float s = %s;" % tr.fval(1, 32),          # the source is read before anything is written
                "ctx->xmm[%d].pd[0] = (double)s;" % d]
    return ["double s = %s;" % tr.fval(1, 64),
            "ctx->xmm[%d].f[0] = (float)s;" % d]
