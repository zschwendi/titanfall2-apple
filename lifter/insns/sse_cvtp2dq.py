"""SSE packed float to signed doubleword conversions: cvttps2dq, cvttpd2dq (truncating) and
cvtps2dq, cvtpd2dq (rounding).

The truncating forms are the packed float formulation of the findings table
(docs/findings/11-float-mode.md, "Exact C"): every lane is range-checked and returns the integer
indefinite (INT32_MIN) for NaN and out of range, never a plain (int)v.  The double forms fill
lanes 0 and 1 and zero the high half.

The rounding forms follow the table's "Exact C" entry for cvtps2dq/cvtpd2dq: round each lane to
an integral value in the rounding mode first (tf_frint32/tf_frint64 of harness/runtime.h, the host
frintx written out, as cvtss2si/cvtsd2si do), then the same range test on the rounded value.  The
table's "not exact" for the naive (int)rintf(v) is why the range test comes after the rounding.
Whether this formulation matches the CPU is judged by make family-check, not assumed here.

Only the register form (x,x) is translated: it is the only form in the game and the corpus, and
the only one tested.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_cvtp2dq.h")


@F.insn("cvttps2dq", enc(0x0F5B, mand=0xF3), vector=True, fp="f32 packed")
def t_cvtt_ps2dq(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None:
        raise Unsupported(tr.name + " memory source: no such instance in the game or corpus, untested")
    out = ["Xmm s = %s;" % tr.xval(1)]                  # copy the source before anything is written
    for j in range(4):
        out.append("ctx->xmm[%d].i[%d] = x_sse_cvtp2dq_t32_f(s.f[%d]);" % (d, j, j))
    return out


@F.insn("cvttpd2dq", enc(0x0FE6, mand=0x66), vector=True, fp="f64 packed")
def t_cvtt_pd2dq(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None:
        raise Unsupported(tr.name + " memory source: no such instance in the game or corpus, untested")
    return ["Xmm s = %s;" % tr.xval(1),                 # copy the source before anything is written
            "ctx->xmm[%d].i[0] = x_sse_cvtp2dq_t32_d(s.pd[0]);" % d,
            "ctx->xmm[%d].i[1] = x_sse_cvtp2dq_t32_d(s.pd[1]);" % d,
            "ctx->xmm[%d].q[1] = 0;" % d]               # the high half of a double-to-dword result is zero


@F.insn("cvtps2dq", enc(0x0F5B, mand=0x66), vector=True, fp="f32 packed")
def t_cvt_ps2dq(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None:
        raise Unsupported(tr.name + " memory source: no such instance in the game or corpus, untested")
    out = ["Xmm s = %s;" % tr.xval(1)]                  # copy the source before anything is written
    for j in range(4):
        out.append("ctx->xmm[%d].i[%d] = x_sse_cvtp2dq_r32_f(s.f[%d]);" % (d, j, j))
    return out


@F.insn("cvtpd2dq", enc(0x0FE6, mand=0xF2), vector=True, fp="f64 packed")
def t_cvt_pd2dq(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    if tr.xr(1) is None:
        raise Unsupported(tr.name + " memory source: no such instance in the game or corpus, untested")
    return ["Xmm s = %s;" % tr.xval(1),                 # copy the source before anything is written
            "ctx->xmm[%d].i[0] = x_sse_cvtp2dq_r32_d(s.pd[0]);" % d,
            "ctx->xmm[%d].i[1] = x_sse_cvtp2dq_r32_d(s.pd[1]);" % d,
            "ctx->xmm[%d].q[1] = 0;" % d]               # the high half of a double-to-dword result is zero
