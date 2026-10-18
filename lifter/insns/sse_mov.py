"""SSE moves and bitwise operations: movaps/movups/movapd/movupd/movdqa/movdqu, the scalar moves
movss and movsd, movd and movq, and the bitwise and/andn/or/xor in their ps, pd and integer
spellings.

Plain C on the Xmm union of harness/runtime.h; nothing here depends on rounding, NaN rules or
MXCSR.  The mandatory prefix is part of each encoding: 0F 10 is movups, with 66 movupd, with
F3 movss, with F2 movsd.  (The string instruction that the decoder also prints as 'movsd' is
movsl here and belongs to the strings family.)
"""

from ..translate import Family, T, Unsupported, enc

F = Family(__name__)


@F.insn("movups", enc(0x0F10, 0x0F11, mand=0), vector=True)
@F.insn("movupd", enc(0x0F10, 0x0F11, mand=0x66), vector=True)
@F.insn("movaps", enc(0x0F28, 0x0F29, mand=0), vector=True)
@F.insn("movapd", enc(0x0F28, 0x0F29, mand=0x66), vector=True)
@F.insn("movdqa", enc(0x0F6F, 0x0F7F, mand=0x66), vector=True)
@F.insn("movdqu", enc(0x0F6F, 0x0F7F, mand=0xF3), vector=True)
def t_movx(tr, flow):
    tr.need_ops(2)
    d, s = tr.xr(0), tr.xr(1)
    if d is not None:
        return ["Xmm v = %s;" % tr.xval(1), "ctx->xmm[%d] = v;" % d]
    if s is not None and tr.is_mem(0):
        return [tr.xstore(0, "ctx->xmm[%d]" % s)]
    raise Unsupported(tr.name + " operand")


@F.insn("movss", enc(0x0F10, 0x0F11, mand=0xF3), vector=True)
@F.insn("movsd", enc(0x0F10, 0x0F11, mand=0xF2), vector=True)
def t_movs_scalar(tr, flow):
    tr.need_ops(2)
    d, s = tr.xr(0), tr.xr(1)
    f, b = ("d", 32) if tr.name == "movss" else ("q", 64)
    if d is not None and s is not None:
        return ["ctx->xmm[%d].%s[0] = ctx->xmm[%d].%s[0];" % (d, f, s, f)]
    if d is not None and tr.is_mem(1):
        return ["%s v = %s(%s);" % (T(b), tr.mac(1, "LD%d" % b), tr.ea(1)),
                "ctx->xmm[%d].q[0] = v;" % d, "ctx->xmm[%d].q[1] = 0;" % d]
    if s is not None and tr.is_mem(0):
        return ["%s(%s, ctx->xmm[%d].%s[0]);" % (tr.mac(0, "ST%d" % b), tr.ea(0), s, f)]
    raise Unsupported(tr.name + " operand")


@F.insn("movd", enc(0x0F6E, 0x0F7E, mand=0x66), vector=True)
@F.insn("movq", enc(0x0F6E, 0x0F7E, 0x0FD6, mand=0x66) | enc(0x0F7E, mand=0xF3), vector=True)
def t_movd_q(tr, flow):
    tr.need_ops(2)
    d, s = tr.xr(0), tr.xr(1)
    b = 32 if tr.name == "movd" else 64
    f = "d" if b == 32 else "q"
    if d is not None and s is not None:
        if b != 64:
            raise Unsupported("movd xmm, xmm")
        return ["uint64_t v = ctx->xmm[%d].q[0];" % s, "ctx->xmm[%d].q[0] = v;" % d, "ctx->xmm[%d].q[1] = 0;" % d]
    if d is not None:
        return ["%s v = %s;" % (T(b), tr.val(1, b)), "ctx->xmm[%d].q[0] = v;" % d, "ctx->xmm[%d].q[1] = 0;" % d]
    if s is not None:
        return [tr.wr(0, "ctx->xmm[%d].%s[0]" % (s, f), b)]
    raise Unsupported(tr.name + " operand")


def t_xbit(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " operand")
    n = tr.name
    if n in ("andnps", "andnpd", "pandn"):
        f = "ctx->xmm[%d].q[%d] = ~ctx->xmm[%d].q[%d] & v.q[%d];"
        return ["Xmm v = %s;" % tr.xval(1)] + [f % (d, k, d, k, k) for k in (0, 1)]
    sym = "^" if "xor" in n else "&" if "and" in n else "|"
    return ["Xmm v = %s;" % tr.xval(1)] + ["ctx->xmm[%d].q[%d] %s= v.q[%d];" % (d, k, sym, k) for k in (0, 1)]


for _n, _op in (("and", 0x0F54), ("andn", 0x0F55), ("or", 0x0F56), ("xor", 0x0F57)):
    F.register(_n + "ps", t_xbit, enc(_op, mand=0), vector=True)
    F.register(_n + "pd", t_xbit, enc(_op, mand=0x66), vector=True)
for _n, _op in (("pand", 0x0FDB), ("pandn", 0x0FDF), ("por", 0x0FEB), ("pxor", 0x0FEF)):
    F.register(_n, t_xbit, enc(_op, mand=0x66), vector=True)
