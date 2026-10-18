"""SSE scalar float minimum and maximum: minss, maxss, minsd, maxsd.

x86: if either operand is a NaN, or both are zeros, the result is the second operand (the
source); otherwise the smaller (larger) one.  Under MXCSR.DAZ a denormal operand counts as a zero
of its sign, and a zero is what comes back when it is selected.

The translation is the host's own fmin/fmax instruction, written out by tf_fmin32..tf_fmax64 of
harness/runtime.h: in the guest float mode it has exactly these rules, measured against a real
Intel CPU (harness/oraclefix.py, "min and max under DAZ").  Not `a < b ? a : b` (it returns the
denormal under DAZ) and never libm's fmin/fmax (they return the operand that is not a NaN).
The first argument is the x86 destination, the second the source; the order matters.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("minss", enc(0x0F5D, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("maxss", enc(0x0F5F, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("minsd", enc(0x0F5D, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("maxsd", enc(0x0F5F, mand=0xF2), vector=True, fp="f64 scalar")
def t_minmax_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    bits, lane, ctype = (32, "f", "float") if tr.name.endswith("ss") else (64, "pd", "double")
    return ["%s s = %s;" % (ctype, tr.fval(1, bits)),              # the source is read before anything is written
            "ctx->xmm[%d].%s[0] = tf_f%s%d(ctx->xmm[%d].%s[0], s);" % (d, lane, tr.name[:3], bits, d, lane)]
