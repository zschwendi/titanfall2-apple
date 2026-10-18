"""Atomics: xchg, xadd, cmpxchg, cmpxchg16b (docs/DESIGN.md section 7).

xchg with a memory operand is always atomic; the others are when they carry a LOCK prefix.
Every atomic form goes through the x_atomic_* helpers of harness/runtime.h, and the core
checks that it did (the atomicity assertion in Translator.translate).  The lock-prefixed forms
of the arithmetic and bit instructions are translated by the families that own those mnemonics.
"""

from ..translate import ATOMIC, Family, T, Unsupported, enc

F = Family(__name__)


@F.insn("xchg", enc(0x86, 0x87, *range(0x90, 0x98)))
def t_xchg(tr, flow):
    tr.need_ops(2)
    b = tr.bits(0)
    mi = 0 if tr.is_mem(0) else 1 if tr.is_mem(1) else None
    if mi is None:
        return ["%s a = %s;" % (T(b), tr.val(0, b)), "%s b = %s;" % (T(b), tr.val(1, b)),
                tr.wr(0, "b"), tr.wr(1, "a")]
    ri = 1 - mi
    tr.lock_ok = True          # xchg with memory is always locked
    p = tr.ea(mi)
    return ["%s v = %s;" % (T(b), tr.val(ri, b)),
            "%s old = %sxchg%d(%s, v);" % (T(b), ATOMIC, b, p),
            tr.wr(ri, "old")]


@F.insn("xadd", enc(0x0FC0, 0x0FC1))
def t_xadd(tr, flow):
    tr.need_ops(2)
    b = tr.bits(0)
    t = T(b)
    if tr.lock:
        if not tr.is_mem(0):
            raise Unsupported("lock xadd reg")
        tr.lock_ok = True
        return ["%s b = %s;" % (t, tr.val(1, b)),
                "%s a = %sadd%d(%s, b);" % (t, ATOMIC, b, tr.ea(0)),
                "(void)x_add%d(ctx, a, b);" % b, tr.wr(1, "a")]
    return ["%s a = %s;" % (t, tr.val(0, b)), "%s b = %s;" % (t, tr.val(1, b)),
            "%s r = x_add%d(ctx, a, b);" % (t, b), tr.wr(1, "a"), tr.wr(0, "r")]


@F.insn("cmpxchg", enc(0x0FB0, 0x0FB1))
def t_cmpxchg(tr, flow):
    # SDM CMPXCHG: flags as CMP(accumulator, dest).  Equal: dest <- src.  Not equal:
    # accumulator <- dest.  With a 32-bit operand only the register that is actually
    # written is zero-extended.
    tr.need_ops(2)
    b = tr.bits(0)
    t = T(b)
    acc = "R%d(0)" % b
    wacc = "W%d(0, %%s);" % b
    if tr.lock:
        if not tr.is_mem(0):
            raise Unsupported("lock cmpxchg reg")
        tr.lock_ok = True
        return ["%s acc = %s;" % (t, acc), "%s s = %s;" % (t, tr.val(1, b)),
                "%s old = %scmpxchg%d(%s, acc, s);" % (t, ATOMIC, b, tr.ea(0)),
                "(void)x_sub%d(ctx, acc, old);" % b,
                "if (old != acc) { %s }" % (wacc % "old")]
    return ["%s acc = %s;" % (t, acc), "%s d = %s;" % (t, tr.val(0, b)), "%s s = %s;" % (t, tr.val(1, b)),
            "(void)x_sub%d(ctx, acc, d);" % b,
            "if (acc == d) { %s } else { %s }" % (tr.wr(0, "s"), wacc % "d")]


@F.insn("cmpxchg16b", enc(0x0FC7, reg=1))
def t_cmpxchg16b(tr, flow):
    # Intel SDM, CMPXCHG8B/CMPXCHG16B: "Note that CMPXCHG16B requires that the destination
    # (memory) operand be 16-byte aligned", and under 64-Bit Mode Exceptions, #GP(0): "If
    # memory operand for CMPXCHG16B is not aligned on a 16-byte boundary."  That holds
    # with and without LOCK, so a misaligned operand is a guest fault like #DE: trap at
    # this instruction, state untouched.
    tr.need_ops(1)
    if not tr.is_mem(0):
        raise Unsupported("cmpxchg16b operand")
    p = tr.ea(0)
    gp = "if (%s & 15) TRAP(0x%x);" % (p, tr.ins.address)
    if tr.lock:
        tr.lock_ok = True
        return [gp,
                "unsigned __int128 old = ((unsigned __int128)R64(2) << 64) | R64(0);",
                "unsigned __int128 des = ((unsigned __int128)R64(1) << 64) | R64(3);",
                "int ok = %scmpxchg128(%s, &old, des);" % (ATOMIC, p),
                "ctx->zf = (uint8_t)ok;",
                "if (!ok) { W64(0, (uint64_t)old); W64(2, (uint64_t)(old >> 64)); }"]
    return [gp,
            "uint64_t lo = %s(%s), hi = %s(%s + 8);" % (tr.mac(0, "LD64"), p, tr.mac(0, "LD64"), p),
            "if (lo == R64(0) && hi == R64(2)) { %s(%s, R64(3)); %s(%s + 8, R64(1)); ctx->zf = 1; }" % (tr.mac(0, "ST64"), p, tr.mac(0, "ST64"), p),
            "else { ctx->zf = 0; W64(0, lo); W64(2, hi); }"]
