"""Integer arithmetic and logic: add, adc, sub, sbb, cmp, and, or, xor, test, inc, dec, neg, not,
mul, imul, div, idiv.

The lock-prefixed forms of add, sub, and, or, xor, inc, dec and not are translated here too,
through the x_atomic_* helpers of harness/runtime.h.  The flag helpers that only these
translators call are in harness/insns/int_arith.h; x_add and x_sub, which other families use as
well (xadd, cmpxchg, the string compares), are in harness/runtime.h.
"""

from ..translate import ATOMIC, Family, T, Unsupported, enc

F = Family(__name__, header="int_arith.h")


def t_bin(tr, flow):
    tr.need_ops(2)
    op, b = tr.name, tr.bits(0)
    t = T(b)
    sym = {"and": "&", "or": "|", "xor": "^", "test": "&"}
    if tr.lock:
        if not tr.is_mem(0) or op not in ("add", "sub", "and", "or", "xor"):
            raise Unsupported("lock " + op)
        tr.lock_ok = True
        lines = ["%s b = %s;" % (t, tr.val(1, b)),
                 "%s a = %s%s%d(%s, b);" % (t, ATOMIC, op, b, tr.ea(0))]
        if op in ("add", "sub"):
            lines.append("(void)x_%s%d(ctx, a, b);" % (op, b))
        else:
            lines.append("(void)x_logic%d(ctx, (%s)(a %s b));" % (b, t, sym[op]))
        return lines
    lines = ["%s a = %s;" % (t, tr.val(0, b)), "%s b = %s;" % (t, tr.val(1, b))]
    if op in ("add", "adc", "sub", "sbb"):
        lines += ["%s r = x_%s%d(ctx, a, b);" % (t, op, b), tr.wr(0, "r")]
    elif op in ("and", "or", "xor"):
        lines += ["%s r = x_logic%d(ctx, (%s)(a %s b));" % (t, b, t, sym[op]), tr.wr(0, "r")]
    elif op == "cmp":
        lines += ["(void)x_sub%d(ctx, a, b);" % b]
    elif op == "test":
        lines += ["(void)x_logic%d(ctx, (%s)(a & b));" % (b, t)]
    else:
        raise Unsupported(op)
    return lines


for _i, _n in enumerate(("add", "or", "adc", "sbb", "and", "sub", "xor", "cmp")):
    F.register(_n, t_bin, enc(*range(8 * _i, 8 * _i + 6)) | enc(0x80, 0x81, 0x83, reg=_i))
F.register("test", t_bin, enc(0x84, 0x85, 0xA8, 0xA9) | enc(0xF6, 0xF7, reg=0) | enc(0xF6, 0xF7, reg=1))


@F.insn("inc", enc(0xFE, 0xFF, reg=0))
@F.insn("dec", enc(0xFE, 0xFF, reg=1))
@F.insn("not", enc(0xF6, 0xF7, reg=2))
@F.insn("neg", enc(0xF6, 0xF7, reg=3))
def t_unary(tr, flow):
    tr.need_ops(1)
    op, b = tr.name, tr.bits(0)
    t = T(b)
    if tr.lock:
        # 'lock neg' is not translated: the oracle computes its SF and PF wrongly and the
        # other four flags cannot tell a right translation from a wrong one, so nothing
        # could check it (review finding 11).  No game module contains one.
        if not tr.is_mem(0) or op == "neg":
            raise Unsupported("lock " + op)
        tr.lock_ok = True
        p = tr.ea(0)
        if op == "inc":
            return ["%s a = %sadd%d(%s, 1);" % (t, ATOMIC, b, p), "(void)x_inc%d(ctx, a);" % b]
        if op == "dec":
            return ["%s a = %ssub%d(%s, 1);" % (t, ATOMIC, b, p), "(void)x_dec%d(ctx, a);" % b]
        return ["(void)%sxor%d(%s, (%s)~(%s)0);" % (ATOMIC, b, p, t, t)]
    if op == "not":
        return [tr.wr(0, "(%s)~%s" % (t, tr.val(0, b)))]
    return ["%s a = %s;" % (t, tr.val(0, b)), "%s r = x_%s%d(ctx, a);" % (t, op, b), tr.wr(0, "r")]


@F.insn("imul", enc(0xF6, 0xF7, reg=5) | enc(0x0FAF, 0x69, 0x6B))
def t_imul(tr, flow):
    tr.need_ops(1, 2, 3)
    b = tr.bits(0)
    if len(tr.ops) == 1:
        return ["x_imul1_%d(ctx, %s);" % (b, tr.val(0, b))]
    if b == 8:
        raise Unsupported("imul 8-bit multi-operand")
    if len(tr.ops) == 2:
        a, c = tr.val(0, b), tr.val(1, b)
    else:
        a, c = tr.val(1, b), tr.val(2, b)
    return ["%s r = x_imul%d(ctx, %s, %s);" % (T(b), b, a, c), tr.wr(0, "r")]


@F.insn("mul", enc(0xF6, 0xF7, reg=4))
def t_mul(tr, flow):
    tr.need_ops(1)
    b = tr.bits(0)
    return ["x_mul1_%d(ctx, %s);" % (b, tr.val(0, b))]


@F.insn("div", enc(0xF6, 0xF7, reg=6))
@F.insn("idiv", enc(0xF6, 0xF7, reg=7))
def t_div(tr, flow):
    tr.need_ops(1)
    b = tr.bits(0)
    return ["if (x_%s%d(ctx, %s)) TRAP(0x%x);" % (tr.name, b, tr.val(0, b), tr.ins.address)]
