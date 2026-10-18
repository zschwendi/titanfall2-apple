"""Shifts and bit operations: shl/sal, shr, sar, rol, ror, shld, shrd, bt, bts, btr, btc, bsf, bsr.

The lock-prefixed forms of bts, btr and btc with a memory operand are translated here too,
through the x_atomic_* helpers of harness/runtime.h.  The shift and rotate helpers are in
harness/insns/shift_bit.h.
"""

from capstone import x86_const as X

from ..translate import ATOMIC, Family, T, Unsupported, enc

F = Family(__name__, header="shift_bit.h")


def _count(tr, i):
    if i >= len(tr.ops):
        return "1"
    op = tr.ops[i]
    if op.type == X.X86_OP_IMM:
        return "%d" % (op.imm & 0xFF)
    if op.type == X.X86_OP_REG and op.reg == X.X86_REG_CL:
        return "R8(1)"
    raise Unsupported("%s count operand" % tr.name)


def t_shift(tr, flow):
    tr.need_ops(1, 2)
    op = {"sal": "shl"}.get(tr.name, tr.name)
    b = tr.bits(0)
    return ["%s a = %s;" % (T(b), tr.val(0, b)),
            "%s r = x_%s%d(ctx, a, (uint8_t)%s);" % (T(b), op, b, _count(tr, 1)),
            tr.wr(0, "r")]


_SHIFT = (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3)
for _regs, _n in (((0,), "rol"), ((1,), "ror"), ((4, 6), "shl"), ((4, 6), "sal"), ((5,), "shr"), ((7,), "sar")):
    F.register(_n, t_shift, set().union(*[enc(*_SHIFT, reg=_r) for _r in _regs]))


@F.insn("shld", enc(0x0FA4, 0x0FA5))
@F.insn("shrd", enc(0x0FAC, 0x0FAD))
def t_dshift(tr, flow):
    tr.need_ops(3)
    b = tr.bits(0)
    if b not in (16, 32, 64):
        raise Unsupported(tr.name + " size")
    return ["%s a = %s;" % (T(b), tr.val(0, b)), "%s b = %s;" % (T(b), tr.val(1, b)),
            "%s r = x_%s%d(ctx, a, b, (uint8_t)%s);" % (T(b), tr.name, b, _count(tr, 2)),
            tr.wr(0, "r")]


@F.insn("bt", enc(0x0FA3) | enc(0x0FBA, reg=4))
@F.insn("bts", enc(0x0FAB) | enc(0x0FBA, reg=5))
@F.insn("btr", enc(0x0FB3) | enc(0x0FBA, reg=6))
@F.insn("btc", enc(0x0FBB) | enc(0x0FBA, reg=7))
def t_bt(tr, flow):
    tr.need_ops(2)
    op, b = tr.name, tr.bits(0)
    t = T(b)
    mod = {"bts": "(%s)(a | bit)", "btr": "(%s)(a & (%s)~bit)", "btc": "(%s)(a ^ bit)"}
    o = tr.ops[1]
    if o.type == X.X86_OP_IMM:
        off = "%d" % (o.imm & 0xFF)
    else:
        off = tr.val(1, b)
    if tr.is_reg(0):
        if tr.lock:
            raise Unsupported("lock " + op)
        lines = ["%s a = %s;" % (t, tr.val(0, b)), "unsigned n = (unsigned)(%s) & %d;" % (off, b - 1),
                 "%s bit = (%s)((%s)1 << n);" % (t, t, t), "ctx->cf = (uint8_t)((a >> n) & 1);"]
        if op != "bt":
            e = mod[op] % ((t, t) if op == "btr" else (t,))
            lines.append(tr.wr(0, e))
        return lines
    # memory bit string: a register offset selects the unit, signed (SDM BT "Operation")
    if o.type == X.X86_OP_IMM:
        lines = ["uint64_t p = %s;" % tr.ea(0), "unsigned n = %d;" % ((o.imm & 0xFF) & (b - 1))]
    else:
        k = {16: 4, 32: 5, 64: 6}[b]
        lines = ["int64_t o = (int64_t)(int%d_t)%s;" % (b, off),
                 "uint64_t p = %s + (uint64_t)(o >> %d) * %d;" % (tr.ea(0), k, b // 8),
                 "unsigned n = (unsigned)o & %d;" % (b - 1)]
    lines.append("%s bit = (%s)((%s)1 << n);" % (t, t, t))
    if op == "bt":
        if tr.lock:
            raise Unsupported("lock bt")
        lines.append("%s a = %s(p);" % (t, tr.mac(0, "LD%d" % b)))
    elif tr.lock:
        tr.lock_ok = True
        f = {"bts": "%sor%d(p, bit)" % (ATOMIC, b), "btr": "%sand%d(p, (%s)~bit)" % (ATOMIC, b, t),
             "btc": "%sxor%d(p, bit)" % (ATOMIC, b)}[op]
        lines.append("%s a = %s;" % (t, f))
    else:
        e = mod[op] % ((t, t) if op == "btr" else (t,))
        lines += ["%s a = %s(p);" % (t, tr.mac(0, "LD%d" % b)), "%s(p, %s);" % (tr.mac(0, "ST%d" % b), e)]
    lines.append("ctx->cf = (uint8_t)((a >> n) & 1);")
    return lines


# F3 0F BC/BD are tzcnt/lzcnt: never F2/F3 on these opcodes
@F.insn("bsf", enc(0x0FBC, mand=0) | enc(0x0FBC, mand=0x66))
@F.insn("bsr", enc(0x0FBD, mand=0) | enc(0x0FBD, mand=0x66))
def t_bscan(tr, flow):
    tr.need_ops(2)
    b = tr.bits(0)
    idx = "__builtin_ctzll((uint64_t)s)" if tr.name == "bsf" else "(63 - __builtin_clzll((uint64_t)s))"
    # Source 0: ZF = 1 and the destination is undefined (SDM); it is left unchanged here.
    return ["%s s = %s;" % (T(b), tr.val(1, b)),
            "if (s == 0) { ctx->zf = 1; } else { ctx->zf = 0; %s }" % tr.wr(0, "(%s)%s" % (T(b), idx))]
