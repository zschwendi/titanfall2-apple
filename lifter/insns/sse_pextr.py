"""SSE lane extracts: pextrw, pextrd and pextrq (66 0F C5 ib, 66 0F 3A 15 ib, 66 0F 3A 16 ib).

Each copies one lane of an xmm register (operand 1) to a general register or to memory (operand
0); the immediate (operand 2) picks the lane. No C helpers: a lane is an index into ctx->xmm.
The memory forms of pextrw (m16) and pextrq (m64) are refused: no instance of the game or of the
corpus has them, so they cannot be tested.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


def _lane(tr, lanes):
    """Source xmm register (operand 1) and lane index (operand 2 & (lanes - 1)); refuses any other form."""
    tr.need_ops(3)
    s = tr.xr(1)
    if s is None or tr.is_reg(2) or tr.is_mem(2):
        raise Unsupported(tr.name + " operand")
    return s, tr.ops[2].imm & (lanes - 1)


@F.insn("pextrw", enc(0x0FC5, mand=0x66) | enc(0x0F3A15, mand=0x66), vector=True)
def t_pextrw(tr, flow):
    s, k = _lane(tr, 8)
    if not tr.is_reg(0) or tr.bits(0) != 32:          # r32 only; the memory form is refused
        raise Unsupported("pextrw destination")
    # a 32-bit register write clears bits 63:32, so the word is zero-extended to the whole register
    return [tr.wr(0, "(uint32_t)ctx->xmm[%d].w[%d]" % (s, k), 32)]


@F.insn("pextrd", enc(0x0F3A16, mand=0x66), vector=True)
def t_pextrd(tr, flow):
    s, k = _lane(tr, 4)
    if not (tr.is_reg(0) or tr.is_mem(0)) or tr.bits(0) != 32:     # r32 or m32
        raise Unsupported("pextrd destination")
    return [tr.wr(0, "ctx->xmm[%d].d[%d]" % (s, k), 32)]


@F.insn("pextrq", enc(0x0F3A16, mand=0x66), vector=True)
def t_pextrq(tr, flow):
    s, k = _lane(tr, 2)
    if not tr.is_reg(0) or tr.bits(0) != 64:          # r64 only; the memory form is refused
        raise Unsupported("pextrq destination")
    return [tr.wr(0, "ctx->xmm[%d].q[%d]" % (s, k), 64)]
