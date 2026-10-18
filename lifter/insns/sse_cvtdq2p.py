"""SSE packed integer to float conversions: cvtdq2ps, cvtdq2pd.

Declared fp="f32 packed" (cvtdq2ps writes four singles) and fp="f64 packed" (cvtdq2pd writes two
doubles), as cvtsi2ss/cvtsi2sd are declared by the type they write: the harness then runs every
case under the seven MXCSR settings and takes the expected result from the real CPU
(harness/floatcase.py).

The translation is the scalar conversion of docs/findings/11-float-mode.md, lane by lane:
(float)(int32_t) for cvtdq2ps, which honours the rounding mode (the table's cvtsi2ss row, 4 RC
exact, and its rounding row, which names cvtdq2ps), and (double)(int32_t) for cvtdq2pd, exact in
every mode.  The source is read whole before anything is written (it may be the destination).

A 16-byte source of cvtdq2ps may be memory, and x86 then requires it to be 16-byte aligned:
tr.xval(1) emits that fault before the load.  The source of cvtdq2pd is 8 bytes of memory with no
alignment rule: its two lanes are read with LD32 at tr.ea(1) and tr.ea(1) + 4.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("cvtdq2ps", enc(0x0F5B, mand=0), vector=True, fp="f32 packed")
def t_cvtdq2ps(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported("cvtdq2ps destination")
    out = ["Xmm s = %s;" % tr.xval(1)]                  # an xmm register or an aligned m128, read whole first
    for j in range(4):
        out.append("ctx->xmm[%d].f[%d] = (float)s.i[%d];" % (d, j, j))
    return out


@F.insn("cvtdq2pd", enc(0x0FE6, mand=0xF3), vector=True, fp="f64 packed")
def t_cvtdq2pd(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported("cvtdq2pd destination")
    if tr.is_mem(1):
        if tr.bits(1) != 64:
            raise Unsupported("cvtdq2pd source is not m64")
        m = tr.ea(1)                                   # 8 bytes, no alignment rule: two lanes read first
        return ["int32_t a = (int32_t)%s(%s);" % (tr.mac(1, "LD32"), m),
                "int32_t b = (int32_t)%s(%s + 4);" % (tr.mac(1, "LD32"), m),
                "ctx->xmm[%d].pd[0] = (double)a;" % d,
                "ctx->xmm[%d].pd[1] = (double)b;" % d]
    if tr.xr(1) is None:
        raise Unsupported("cvtdq2pd source")
    return ["Xmm s = %s;" % tr.xval(1),                # the xmm source, copied before anything is written
            "ctx->xmm[%d].pd[0] = (double)s.i[0];" % d,
            "ctx->xmm[%d].pd[1] = (double)s.i[1];" % d]
