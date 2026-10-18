"""SSE packed float add and subtract: addps, addpd, subps, subpd.  The pattern for a packed float family.

Declared float with fp="f32 packed" / "f64 packed": the harness then gives every lane float-aware
operands, runs every case under the seven MXCSR settings and takes the expected result from the
real CPU (harness/floatcase.py).

The translation is the one docs/findings/11-float-mode.md measured exact in the guest float
mode, done on the whole register by one host vector instruction (harness/runtime.h, THE PACKED
FORMS: the host's vector fadd and fsub are its scalar ones in every lane), on a copy of the
source taken before anything is written (the source may be the destination).  The helpers keep
the operand order (with two NaN operands x86 returns the first one, the destination's lane,
quieted).

The source may be 16 bytes of memory, and x86 then requires it to be 16-byte aligned: #GP
otherwise.  tr.xval(1) is the whole of it: it emits that fault before the load.  Never read such
an operand with LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("addps", enc(0x0F58, mand=0), vector=True, fp="f32 packed")
@F.insn("addpd", enc(0x0F58, mand=0x66), vector=True, fp="f64 packed")
@F.insn("subps", enc(0x0F5C, mand=0), vector=True, fp="f32 packed")
@F.insn("subpd", enc(0x0F5C, mand=0x66), vector=True, fp="f64 packed")
def t_addsub_packed(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    # One host vector instruction for the whole register (harness/runtime.h, THE PACKED FORMS):
    # tf_fadd_ps, tf_fadd_pd, tf_fsub_ps, tf_fsub_pd.  a = the x86 destination, b = the source.
    fn = "tf_f%s_%s" % (tr.name[:3], tr.name[3:])
    return ["Xmm s = %s;" % tr.xval(1),                 # an xmm register or an aligned m128, read whole first
            "ctx->xmm[%d] = %s(ctx->xmm[%d], s);" % (d, fn, d)]
