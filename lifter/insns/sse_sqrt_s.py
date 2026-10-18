"""SSE scalar square root: sqrtss, sqrtsd.

The formulation is the row "sqrtss/sd/ps/pd" of docs/findings/11-float-mode.md, measured exact in
the guest float mode (harness/fpmode.h): the host's fsqrt on the single and on the double, written
out as tf_fsqrt32 and tf_fsqrt64 (harness/runtime.h) so that no build flag can turn it into a call
of the host's libm. Lane 0 is written, upper lanes are untouched. The source is read before the
destination is written, because they may be the same register.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_sqrt_s.h")


@F.insn("sqrtss", enc(0x0F51, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("sqrtsd", enc(0x0F51, mand=0xF2), vector=True, fp="f64 scalar")
def t_sqrt_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.name == "sqrtss":
        return ["float s = %s;" % tr.fval(1, 32),                 # the source is read before anything is written
                "ctx->xmm[%d].f[0] = tf_fsqrt32(s);" % d]
    return ["double s = %s;" % tr.fval(1, 64),
            "ctx->xmm[%d].pd[0] = tf_fsqrt64(s);" % d]
