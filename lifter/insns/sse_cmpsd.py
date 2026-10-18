"""SSE2 scalar double compares, the predicate in the mnemonic: cmpeqsd ... cmpordsd.

CMPSD xmm1, xmm2/m64, imm8 with imm8 = 0..7 compares the low doubles, dest = dest OP src, and
writes an all-ones or all-zeros mask to the low quadword; the high quadword is kept.  The decoder
prints the predicate in the name, so there is one mnemonic per predicate and no immediate operand.

The eight C conditions are the ones measured exact in the guest float mode (docs/findings/11-float-mode.md,
"cmp{eq,lt,le,unord,neq,nlt,nle,ord} ss sd ps pd").  Compares are written as !(a < b), never a >= b.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)

# mnemonic -> C condition on a (x86 destination) and b (source)
PREDICATE = {
    "cmpeqsd": "a == b",
    "cmpltsd": "a < b",
    "cmplesd": "a <= b",
    "cmpunordsd": "(a != a) | (b != b)",
    "cmpneqsd": "!(a == b)",
    "cmpnltsd": "!(a < b)",
    "cmpnlesd": "!(a <= b)",
    "cmpordsd": "(a == a) & (b == b)",
}


@F.insn("cmpeqsd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmpltsd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmplesd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmpunordsd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmpneqsd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmpnltsd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmpnlesd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
@F.insn("cmpordsd", enc(0x0FC2, mand=0xF2), vector=True, fp="f64 scalar")
def t_cmp_sd(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    return ["double a = ctx->xmm[%d].pd[0];" % d,              # both operands are read before anything is written
            "double b = %s;" % tr.fval(1, 64),
            "ctx->xmm[%d].q[0] = (%s) ? ~(uint64_t)0 : (uint64_t)0;" % (d, PREDICATE[tr.name])]
