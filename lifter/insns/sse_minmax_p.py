"""SSE packed float minimum and maximum: minps, maxps, minpd, maxpd.

Declared float with fp="f32 packed" / "f64 packed": the harness gives every lane float-aware
operands, runs every case under the seven MXCSR settings and takes the expected result from the
real CPU (harness/floatcase.py).

The translation is one host vector instruction on the whole register (harness/runtime.h, THE
PACKED FORMS: tf_fmin_ps, tf_fmin_pd, tf_fmax_ps, tf_fmax_pd), on a copy of the source taken
before anything is written (the source may be the destination).  The first argument is the x86
destination, the second the source: the order decides what a NaN or two zeros return, as in the
scalar minss/maxss form (docs/findings/11-float-mode.md: x86 returns the second operand for a NaN
and for two zeros, and under DAZ a zero where the operand it selects is a denormal).  The host's
vector fmin and fmax do exactly that lane by lane in the guest float mode.  Never a compare-select,
never libm.

The source may be 16 bytes of memory, and x86 then requires it to be 16-byte aligned: #GP
otherwise.  tr.xval(1) is the whole of it: it emits that fault before the load.  Never read such
an operand with LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("minps", enc(0x0F5D, mand=0), vector=True, fp="f32 packed")
@F.insn("maxps", enc(0x0F5F, mand=0), vector=True, fp="f32 packed")
@F.insn("minpd", enc(0x0F5D, mand=0x66), vector=True, fp="f64 packed")
@F.insn("maxpd", enc(0x0F5F, mand=0x66), vector=True, fp="f64 packed")
def t_minmax_packed(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    # One host vector instruction for the whole register: tf_fmin_ps/_pd, tf_fmax_ps/_pd.
    # a = the x86 destination, b = the source.
    fn = "tf_f%s_%s" % (tr.name[:3], tr.name[3:])
    return ["Xmm s = %s;" % tr.xval(1),                 # an xmm register or an aligned m128, read whole first
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, fn, d)]
