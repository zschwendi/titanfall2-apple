"""SSE2 packed double compares: cmpeqpd, cmpltpd, cmplepd, cmpunordpd, cmpneqpd, cmpnltpd, cmpnlepd, cmpordpd.

The decoder prints CMPPD with an immediate 0..7 under the predicate's name and shows no
immediate: cmpeqpd is predicate 0 (EQ), cmpltpd 1 (LT), cmplepd 2 (LE), cmpunordpd 3 (UNORD),
cmpneqpd 4 (NEQ), cmpnltpd 5 (NLT), cmpnlepd 6 (NLE), cmpordpd 7 (ORD).  Each destination lane
becomes all ones where the predicate holds for the two doubles, all zeros where it does not.

Declared float with fp="f64 packed": the harness then gives every lane float-aware operands, runs
every case under the seven MXCSR settings and takes the expected result from the real CPU.
The formulations are the ones measured exact in the guest float mode
(docs/findings/11-float-mode.md): the C comparison of the two lanes, written with ==, < and <=,
and with ! for the negated predicates, never >= (it differs on NaN).  Unordered is a NaN in
either operand; ord is its complement.

The source is copied before anything is written (the source may be the destination), and it may be
16 bytes of memory: tr.xval(1) then emits the #GP before the load, as x86 does.  Never read such
an operand with LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)

# The C test of one lane: a = the destination double, b = the source double.
TEST = {
    "cmpeqpd": "a == b",
    "cmpltpd": "a < b",
    "cmplepd": "a <= b",
    "cmpunordpd": "(a != a) | (b != b)",
    "cmpneqpd": "!(a == b)",
    "cmpnltpd": "!(a < b)",
    "cmpnlepd": "!(a <= b)",
    "cmpordpd": "(a == a) & (b == b)",
}


@F.insn("cmpeqpd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmpltpd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmplepd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmpunordpd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmpneqpd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmpnltpd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmpnlepd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
@F.insn("cmpordpd", enc(0x0FC2, mand=0x66), vector=True, fp="f64 packed")
def t_cmp_packed_pd(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    test = TEST[tr.name]
    out = ["Xmm s = %s;" % tr.xval(1)]                  # an xmm register or an aligned m128, read whole first
    for j in range(2):
        out.append("{ double a = ctx->xmm[%d].pd[%d], b = s.pd[%d]; "
                   "ctx->xmm[%d].q[%d] = (%s) ? ~(uint64_t)0 : (uint64_t)0; }" % (d, j, j, d, j, test))
    return out
