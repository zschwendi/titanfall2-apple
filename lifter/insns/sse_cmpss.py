"""SSE scalar float compares: cmpeqss, cmpltss, cmpless, cmpunordss, cmpneqss, cmpnltss, cmpnless, cmpordss.

CMPSS with imm8 0..7: the predicate is in the mnemonic (the decoder prints no immediate). The low
single of the destination is compared with the source, and its low 32 bits become all ones for
true or zero for false; bits 127:32 of the destination stay. The eight predicates are the
Intel ones: EQ_OQ, LT_OS, LE_OS, UNORD_Q, NEQ_UQ, NLT_US, NLE_US, ORD_Q.

A compare depends on NaN rules, so it is declared as a float mnemonic. The C formulations are the
ones docs/findings/11-float-mode.md measured exact in the guest float mode (row cmp{eq,...}):
plain comparisons, with the negated predicates written as !(a < b) and never a >= b.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)

# the C condition on a (the destination lane) and b (the source lane), per mnemonic
_COND = {
    "cmpeqss": "(a == b)",                 # EQ_OQ: false on NaN
    "cmpltss": "(a < b)",                  # LT_OS
    "cmpless": "(a <= b)",                 # LE_OS
    "cmpunordss": "(a != a || b != b)",    # UNORD_Q
    "cmpneqss": "!(a == b)",               # NEQ_UQ: true on NaN
    "cmpnltss": "!(a < b)",                # NLT_US
    "cmpnless": "!(a <= b)",               # NLE_US
    "cmpordss": "(a == a && b == b)",      # ORD_Q
}


@F.insn("cmpeqss", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpltss", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpless", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpunordss", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpneqss", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpnltss", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpnless", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("cmpordss", enc(0x0FC2, mand=0xF3), vector=True, fp="f32 scalar")
def t_cmp_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    cond = _COND.get(tr.name)
    if cond is None:
        raise Unsupported(tr.name + " predicate")
    return ["float b = %s;" % tr.fval(1, 32),           # the source is read before anything is written
            "float a = ctx->xmm[%d].f[0];" % d,
            "ctx->xmm[%d].d[0] = %s ? 0xFFFFFFFFu : 0u;" % (d, cond)]
