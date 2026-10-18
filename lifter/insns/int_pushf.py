"""RFLAGS on the stack: pushfq, popfq (9C, 9D).

Ctx holds seven bits of RFLAGS: the six arithmetic flags and DF.  The rest is decided here
(harness/statefix.py has the same rule for the test's expected side):

    pushfq   pushes CF PF AF ZF SF DF OF from Ctx, bit 1 (always 1), IF (bit 9) = 1 as in any
             Windows process in user mode, and 0 in every other bit.
    popfq    pops 8 bytes and loads the seven flags.  A value with TF, NT, AC or ID set, which
             pushfq never stores and lifted code could not honour, is a trap, taken before
             anything changes.  IF, IOPL and the reserved bits are ignored: a user-mode popfq
             does not change the first two.

The 16-bit forms (66 9C, 66 9D: pushf, popf) are not translated.
"""

from ..translate import Family, enc

F = Family(__name__, header="int_pushf.h")


@F.insn("pushfq", enc(0x9C, mand=0))
def t_pushfq(tr, flow):
    tr.need_ops(0)
    return ["uint64_t v = x_pushfq_value(ctx);", "ctx->r[4] -= 8;", "STS64(ctx->r[4], v);"]


@F.insn("popfq", enc(0x9D, mand=0))
def t_popfq(tr, flow):
    tr.need_ops(0)
    return ["uint64_t v = LDS64(ctx->r[4]);",
            "if (v & TF_INT_PUSHF_TRAP) TRAP(0x%x);" % tr.ins.address,
            "ctx->r[4] += 8;",
            "x_popfq_load(ctx, v);"]
