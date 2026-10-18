"""SSE scalar float to integer, rounding in the mode MXCSR selects: cvtss2si, cvtsd2si.

The truncating forms are not here. The source is lane 0 of an xmm register or an m32/m64; it is
rounded first (tf_frint32 for the float, tf_frint64 for the double: the host's frintx, which follows MXCSR.RC), and the range test
is applied to the rounded value. That is the formulation of docs/findings/11-float-mode.md, column
"Exact C": rounding first is what makes cvtsd2si(2147483647.5) come out right under round-to-nearest.
A NaN or a rounded value outside the destination's range gives the integer indefinite
(0x80000000 for a 32-bit destination, 0x8000000000000000 for a 64-bit one), in
harness/insns/sse_cvt_s2si.h. No flags change.
"""

from ..translate import Family, T, Unsupported, enc

F = Family(__name__, header="sse_cvt_s2si.h")


@F.insn("cvtss2si", enc(0x0F2D, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cvtsd2si", enc(0x0F2D, mand=0xF2), vector=True, fp="f64 scalar")
def t_cvt_s2si(tr, flow):
    tr.need_ops(2)
    if not tr.is_reg(0):
        raise Unsupported(tr.name + " destination")
    dbits = tr.bits(0)
    if dbits not in (32, 64):
        raise Unsupported(tr.name + " destination width")
    single = tr.name == "cvtss2si"
    sbits, ctype, rnd = (32, "float", "tf_frint32") if single else (64, "double", "tf_frint64")
    return ["%s s = %s;" % (ctype, tr.fval(1, sbits)),          # the source is read before anything is written
            "double t = %s(s);" % rnd,                          # rounded in the MXCSR mode, exact in a double
            tr.wr(0, "(%s)x_sse_cvt_s2si_%d(t)" % (T(dbits), dbits), dbits)]
