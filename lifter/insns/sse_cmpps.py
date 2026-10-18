"""SSE packed float compares: cmpeqps, cmpltps, cmpleps, cmpunordps, cmpneqps, cmpnltps, cmpnleps, cmpordps.

0F C2 /r ib: the immediate 0..7 is the predicate, and the decoder names the instruction by it (an
immediate above 7 is printed cmpps and stays untranslated).  Each lane of the destination becomes
all ones where the predicate holds for (destination lane, source lane), and all zeros otherwise.

Declared fp="f32 packed": the harness gives every lane float-aware operands and runs every case
under the seven MXCSR settings.  The C tests are the ones of docs/findings/11-float-mode.md
(row "cmp{eq,lt,le,unord,neq,nlt,nle,ord} ss sd ps pd", exact in the guest mode): `<`, `<=`, `==`
and `!=` as written, and `!(a<b)` rather than `a>=b`, which differ on NaN.

The source may be 16 bytes of memory, and x86 then requires it to be 16-byte aligned: #GP
otherwise.  tr.xval(1) emits that fault before the load.  Never read such an operand with
LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)

# mnemonic (the name carries the imm8 predicate 0..7) -> C test of a = the destination lane
# against b = the source lane
PREDICATES = {
    "cmpeqps": "{a} == {b}",
    "cmpltps": "{a} < {b}",
    "cmpleps": "{a} <= {b}",
    "cmpunordps": "({a} != {a}) || ({b} != {b})",
    "cmpneqps": "!({a} == {b})",
    "cmpnltps": "!({a} < {b})",
    "cmpnleps": "!({a} <= {b})",
    "cmpordps": "({a} == {a}) && ({b} == {b})",
}


@F.insn("cmpeqps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpltps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpleps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpunordps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpneqps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpnltps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpnleps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
@F.insn("cmpordps", enc(0x0FC2, mand=0), vector=True, fp="f32 packed")
def t_cmp_packed(tr, flow):
    tr.need_ops(2)                                      # the predicate is in the name: no immediate operand
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    test = PREDICATES[tr.name]
    out = ["Xmm s = %s;" % tr.xval(1)]                  # the source, whole, read before anything is written
    for j in range(4):
        a = "ctx->xmm[%d].f[%d]" % (d, j)
        b = "s.f[%d]" % j
        out.append("ctx->xmm[%d].d[%d] = (%s) ? 0xFFFFFFFFu : 0u;" % (d, j, test.format(a=a, b=b)))
    return out
