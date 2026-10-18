"""SSE packed float multiply and divide: mulps, mulpd, divps, divpd.

Declared fp="f32 packed" / "f64 packed", as sse_addsub_p.py: the harness gives every lane
float-aware operands, runs every case under the seven MXCSR settings and takes the expected
result from the real CPU (harness/floatcase.py).

The translation is one host vector instruction on the whole register (harness/runtime.h, THE
PACKED FORMS: tf_fmul_ps, tf_fmul_pd, tf_fdiv_ps, tf_fdiv_pd), on a copy of the source taken
before anything is written (the source may be the destination).  The helpers keep the operand
order (a = the destination's lane first: with two NaN operands x86 returns the first one,
quieted).  Division is exact in every lane under the guest float mode, as the scalar form was.

The source may be 16 bytes of memory, and x86 then requires it to be 16-byte aligned: #GP
otherwise.  tr.xval(1) is the whole of it: it emits that fault before the load.  Never read such
an operand with LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("mulps", enc(0x0F59, mand=0), vector=True, fp="f32 packed")
@F.insn("mulpd", enc(0x0F59, mand=0x66), vector=True, fp="f64 packed")
@F.insn("divps", enc(0x0F5E, mand=0), vector=True, fp="f32 packed")
@F.insn("divpd", enc(0x0F5E, mand=0x66), vector=True, fp="f64 packed")
def t_muldiv_packed(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    # One host vector instruction for the whole register (harness/runtime.h, THE PACKED FORMS):
    # tf_fmul_ps, tf_fmul_pd, tf_fdiv_ps, tf_fdiv_pd.  a = the x86 destination, b = the source.
    fn = "tf_f%s_%s" % (tr.name[:3], tr.name[3:])
    return ["Xmm s = %s;" % tr.xval(1),                 # an xmm register or an aligned m128, read whole first
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, fn, d)]
