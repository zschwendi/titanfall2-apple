"""SSE insert of one lane into an xmm register: pinsrw, pinsrb, pinsrd (66 0F C4 /r ib,
66 0F 3A 20 /r ib, 66 0F 3A 22 /r ib).

The destination is an xmm register; the source is a general register (r32, low 16, 8 or 32
bits used) or memory of the lane's size; imm8 picks the lane (imm8 & 7 for words, & 15 for
bytes, & 3 for dwords). The other lanes keep their value. No C helpers: one lane is written
in place.
"""

from ..translate import Family, T, Unsupported, enc

F = Family(__name__)


def _insert(tr, lane_bits, lane_count, lane):
    """Insert the source into lane imm8 % lane_count of the xmm destination; refuses any other
    form. lane is the Xmm lane letter for lane_bits (w, b or d)."""
    tr.need_ops(3)
    d = tr.xr(0)
    if d is None or tr.xr(1) is not None or tr.is_reg(2) or tr.is_mem(2):
        raise Unsupported(tr.name + " operand")
    if tr.is_reg(1) and tr.bits(1) == 32:
        src = "(%s)%s" % (T(lane_bits), tr.val(1, 32))     # the low lane_bits of r32
    elif tr.is_mem(1) and tr.bits(1) == lane_bits:
        src = tr.val(1, lane_bits)                         # memory of exactly the lane's size
    else:
        raise Unsupported(tr.name + " source operand")
    n = (tr.ops[2].imm & 0xFF) % lane_count
    return ["%s v = %s;" % (T(lane_bits), src),            # read the source before the write
            "ctx->xmm[%d].%s[%d] = v;" % (d, lane, n)]


@F.insn("pinsrw", enc(0x0FC4, mand=0x66), vector=True)
def t_pinsrw(tr, flow):
    return _insert(tr, 16, 8, "w")


@F.insn("pinsrb", enc(0x0F3A20, mand=0x66), vector=True)
def t_pinsrb(tr, flow):
    return _insert(tr, 8, 16, "b")


@F.insn("pinsrd", enc(0x0F3A22, mand=0x66), vector=True)
def t_pinsrd(tr, flow):
    return _insert(tr, 32, 4, "d")
