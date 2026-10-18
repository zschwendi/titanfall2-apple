"""Data movement: mov, lea, zero and sign extension, bswap, the accumulator sign extensions,
setcc and cmovcc, and the stack instructions push, pop and leave.

None of them changes a flag.  No C helpers of their own: everything is a register or memory
access from harness/runtime.h.
"""

from capstone import x86_const as X

from ..translate import CC, Family, T, Unsupported, enc, lit

F = Family(__name__)


@F.insn("mov", enc(0x88, 0x89, 0x8A, 0x8B, *range(0xB0, 0xC0)) | enc(0xC6, 0xC7, reg=0))
@F.insn("movabs", enc(0xA0, 0xA1, 0xA2, 0xA3, *range(0xB8, 0xC0)))
def t_mov(tr, flow):
    tr.need_ops(2)
    b = tr.bits(0)
    return [tr.wr(0, tr.val(1, b))]


@F.insn("lea", enc(0x8D))
def t_lea(tr, flow):
    tr.need_ops(2)
    b = tr.bits(0) if tr.is_reg(0) else 0
    if b not in (16, 32, 64):
        raise Unsupported("lea destination")
    e = tr.ea_expr(1, noseg=True)
    return [tr.wr(0, e if b == 64 else "(%s)%s" % (T(b), e), b)]


@F.insn("movzx", enc(0x0FB6, 0x0FB7))
def t_movzx(tr, flow):
    tr.need_ops(2)
    d, s = tr.bits(0), tr.bits(1)
    return [tr.wr(0, "(%s)%s" % (T(d), tr.val(1, s)))]


@F.insn("movsx", enc(0x0FBE, 0x0FBF))
@F.insn("movsxd", enc(0x63))
def t_movsx(tr, flow):
    tr.need_ops(2)
    d, s = tr.bits(0), tr.bits(1)
    return [tr.wr(0, "(%s)(int%d_t)(int%d_t)%s" % (T(d), d, s, tr.val(1, s)))]


@F.insn("bswap", enc(*range(0x0FC8, 0x0FD0)))
def t_bswap(tr, flow):
    tr.need_ops(1)
    b = tr.bits(0)
    if b not in (32, 64):
        raise Unsupported("bswap r16")
    return [tr.wr(0, "__builtin_bswap%d(%s)" % (b, tr.val(0, b)))]


@F.insn("cbw", enc(0x98))
@F.insn("cwde", enc(0x98))
@F.insn("cdqe", enc(0x98))
@F.insn("cwd", enc(0x99))
@F.insn("cdq", enc(0x99))
@F.insn("cqo", enc(0x99))
def t_sext_acc(tr, flow):
    return {
        "cbw":  ["W16(0, (uint16_t)(int16_t)(int8_t)R8(0));"],
        "cwde": ["W32(0, (uint32_t)(int32_t)(int16_t)R16(0));"],
        "cdqe": ["W64(0, (uint64_t)(int64_t)(int32_t)R32(0));"],
        "cwd":  ["W16(2, (int16_t)R16(0) < 0 ? 0xFFFF : 0);"],
        "cdq":  ["W32(2, (int32_t)R32(0) < 0 ? UINT32_C(0xFFFFFFFF) : 0);"],
        "cqo":  ["W64(2, (int64_t)R64(0) < 0 ? ~UINT64_C(0) : 0);"],
    }[tr.name]


# ---- conditionals ---------------------------------------------------------------------------------

def t_setcc(tr, flow):
    tr.need_ops(1)
    return [tr.wr(0, "(uint8_t)(%s)" % CC[tr.name[3:]], 8)]


def t_cmovcc(tr, flow):
    # The destination is written in either case, so a 32-bit cmov always zero-extends
    # (SDM CMOVcc, 64-bit mode note).
    tr.need_ops(2)
    b = tr.bits(0)
    return ["%s s = %s;" % (T(b), tr.val(1, b)), "%s d = %s;" % (T(b), tr.val(0, b)),
            tr.wr(0, "(%s) ? s : d" % CC[tr.name[4:]])]


for _i, _cc in enumerate(CC):                 # CC is in condition-code order
    F.register("set" + _cc, t_setcc, enc(0x0F90 + _i))
    F.register("cmov" + _cc, t_cmovcc, enc(0x0F40 + _i))


# ---- stack ----------------------------------------------------------------------------------------

def _no_opsize_prefix(tr):
    if 0x66 in list(tr.ins.prefix):
        raise Unsupported("16-bit " + tr.name)


@F.insn("push", enc(*range(0x50, 0x58), 0x68, 0x6A) | enc(0xFF, reg=6))
def t_push(tr, flow):
    tr.need_ops(1)
    _no_opsize_prefix(tr)
    op = tr.ops[0]
    if op.type == X.X86_OP_IMM:
        v = lit(op.imm, 64)
    else:
        v = tr.val(0, 64)
    return ["uint64_t v = %s;" % v, "ctx->r[4] -= 8;", "STS64(ctx->r[4], v);"]


@F.insn("pop", enc(*range(0x58, 0x60)) | enc(0x8F, reg=0))
def t_pop(tr, flow):
    tr.need_ops(1)
    _no_opsize_prefix(tr)
    lines = ["uint64_t v = LDS64(ctx->r[4]);", "ctx->r[4] += 8;"]
    if tr.is_mem(0):
        # the address is computed after rsp has been incremented (SDM POP)
        if tr.bits(0) != 64:
            raise Unsupported("pop memory size")
        lines.append("%s(%s, v);" % (tr.mac(0, "ST64"), tr.ea_expr(0)))
    else:
        lines.append(tr.wr(0, "v", 64))
    return lines


@F.insn("leave", enc(0xC9))
def t_leave(tr, flow):
    _no_opsize_prefix(tr)
    return ["ctx->r[4] = ctx->r[5];", "ctx->r[5] = LDS64(ctx->r[4]);", "ctx->r[4] += 8;"]
