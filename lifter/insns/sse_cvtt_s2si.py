"""SSE truncating float to integer, scalar: cvttss2si, cvttsd2si.

The result is the source's lane 0, truncated toward zero, into a 32-bit or 64-bit general register.
Plain (int)v is wrong on NaN and on overflow (docs/findings/11-float-mode.md, row cvtt*2si), so the
translation range-checks first and returns the integer indefinite (0x80000000, or
0x8000000000000000 for a 64-bit destination) for NaN and for every value outside the range. The
bounds are the ones that table gives (its "Exact C" column), exact in both guest modes.
"""

from ..translate import Family, Unsupported, T, enc

F = Family(__name__)

# (source bits, destination bits) -> range test on v: the value truncates to a representable integer.
RANGE = {
    (32, 32): "v >= -2147483648.0f && v < 2147483648.0f",
    (32, 64): "v >= -9223372036854775808.0 && v < 9223372036854775808.0",
    (64, 32): "v > -2147483649.0 && v < 2147483648.0",
    (64, 64): "v >= -9223372036854775808.0 && v < 9223372036854775808.0",
}
INDEFINITE = {32: "0x80000000u", 64: "0x8000000000000000ull"}


@F.insn("cvttss2si", enc(0x0F2C, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cvttsd2si", enc(0x0F2C, mand=0xF2), vector=True, fp="f64 scalar")
def t_cvtt_s2si(tr, flow):
    tr.need_ops(2)
    if not tr.is_reg(0):
        raise Unsupported(tr.name + " destination")
    dst = tr.bits(0)
    src = 32 if tr.name == "cvttss2si" else 64
    if dst not in (32, 64):
        raise Unsupported(tr.name + " destination width")
    ctype = "float" if src == 32 else "double"
    return ["%s v = %s;" % (ctype, tr.fval(1, src)),              # the source is read before anything is written
            "%s r = (%s) ? (%s)(int%d_t)v : %s;" % (T(dst), RANGE[(src, dst)], T(dst), dst, INDEFINITE[dst]),
            tr.wr(0, "r", dst)]
