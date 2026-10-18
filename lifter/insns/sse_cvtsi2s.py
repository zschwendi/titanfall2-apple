"""SSE integer to scalar float conversions: cvtsi2ss, cvtsi2sd.

Signed 32-bit or (REX.W) 64-bit integer, register or memory, to the low lane of an xmm register;
the other lanes are left as they were.  The conversion rounds when the integer has more bits than
the mantissa, following the rounding mode.  The guest float mode (docs/findings/11-float-mode.md,
"Exact C") measured `d->f[0] = (float)(int32_t)*g;` exact for both widths and honours RMode, so it
is the whole translation.  The integer is read before anything is written.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("cvtsi2ss", enc(0x0F2A, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cvtsi2sd", enc(0x0F2A, mand=0xF2), vector=True, fp="f64 scalar")
def t_cvtsi2s(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    bits = tr.bits(1)
    if bits not in (32, 64):
        raise Unsupported(tr.name + " source of %d bits" % bits)
    single = tr.name == "cvtsi2ss"
    ctype, lane = ("float", "f[0]") if single else ("double", "pd[0]")
    return ["int%d_t s = (int%d_t)%s;" % (bits, bits, tr.val(1, bits)),   # the integer is read before anything is written
            "ctx->xmm[%d].%s = (%s)s;" % (d, lane, ctype)]
