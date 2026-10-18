"""SSE packed square root: sqrtps, sqrtpd.  A packed float family, declared fp="f32 packed" / "f64 packed".

The translation is the one docs/findings/11-float-mode.md measured exact in the guest float mode
(its "Exact C" column, row sqrtss/sd/ps/pd): the square root of each lane of the source.  It is
done on the whole register by one host vector instruction (harness/runtime.h, THE PACKED FORMS:
tf_fsqrt_ps for sqrtps, tf_fsqrt_pd for sqrtpd), on a copy of the source taken before anything is
written (the source may be the destination).  The host's vector fsqrt is its scalar one in every
lane under the same FPCR, so the lanes are the ones the scalar fsqrt gives.

The source may be 16 bytes of memory, and x86 then requires it to be 16-byte aligned: #GP
otherwise.  tr.xval(1) is the whole of it: it emits that fault before the load.  Never read such
an operand with LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("sqrtps", enc(0x0F51, mand=0), vector=True, fp="f32 packed")
@F.insn("sqrtpd", enc(0x0F51, mand=0x66), vector=True, fp="f64 packed")
def t_sqrt_packed(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    # One host vector instruction for the whole register: tf_fsqrt_ps (sqrtps) or tf_fsqrt_pd (sqrtpd).
    fn = "tf_fsqrt_ps" if tr.name.endswith("ps") else "tf_fsqrt_pd"
    return ["Xmm s = %s;" % tr.xval(1),                 # an xmm register or an aligned m128, read whole first
            "ctx->xmm[%d] = %s(s);" % (d, fn)]
