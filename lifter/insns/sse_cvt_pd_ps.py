"""SSE float to double and double to float conversions: cvtps2pd, cvtpd2ps.

Both are declared packed float mnemonics: the harness gives every lane float-aware operands, runs
every case under the seven MXCSR settings and takes the expected result from the real CPU
(harness/floatcase.py).

The translations are the ones docs/findings/11-float-mode.md measured exact in the guest float
mode (column "Exact C"): one conversion per lane, C's own float <-> double conversion, reading the
whole source before anything is written (the source may be the destination).  cvtpd2ps also
zeroes the high 64 bits of the destination (DEST[127:64] := 0 in the Intel manual).

cvtps2pd reads 8 bytes from memory, so there is no alignment rule and the lanes are read with
LDF32 at tr.ea(1) and tr.ea(1) + 4.  cvtpd2ps reads 16 bytes, which x86 requires aligned; its
memory form is not in the game and is not tested, so it is refused, and only the register form is
translated.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("cvtps2pd", enc(0x0F5A, mand=0), vector=True, fp="f32 packed")
def t_cvtps2pd(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is not None:
        out = ["Xmm s = %s;" % tr.xval(1)]                    # the xmm source, copied before the write
        lo, hi = "s.f[0]", "s.f[1]"
    elif tr.is_mem(1) and tr.bits(1) == 64:
        m = tr.ea(1)                                          # m64: 8 bytes, no alignment rule
        lf = tr.mac(1, "LDF32")
        out = ["float lo = %s(%s);" % (lf, m), "float hi = %s(%s + 4);" % (lf, m)]
        lo, hi = "lo", "hi"
    else:
        raise Unsupported(tr.name + " source")
    return out + [
        "ctx->xmm[%d].pd[0] = (double)%s;" % (d, lo),
        "ctx->xmm[%d].pd[1] = (double)%s;" % (d, hi),
    ]


@F.insn("cvtpd2ps", enc(0x0F5A, mand=0x66), vector=True, fp="f64 packed")
def t_cvtpd2ps(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None:
        raise Unsupported(tr.name + " memory source: 16 bytes, aligned, not in the game, not tested")
    return [
        "Xmm s = %s;" % tr.xval(1),                           # read the whole source first
        "ctx->xmm[%d].f[0] = (float)s.pd[0];" % d,
        "ctx->xmm[%d].f[1] = (float)s.pd[1];" % d,
        "ctx->xmm[%d].q[1] = 0;" % d,                         # lanes 2 and 3 (bits 127:64) become zero
    ]
