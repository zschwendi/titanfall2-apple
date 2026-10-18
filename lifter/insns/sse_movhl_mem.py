"""SSE and SSE2 moves of 8 bytes between memory and one half of an xmm register: movhps, movlps,
movhpd and movlpd (0F 16/17 and 0F 12/13, with 66 for the pd forms). The register-register forms
of 0F 12 and 0F 16 are movhlps and movlhps, which another family translates."""

from ..translate import Family, Unsupported, enc

F = Family(__name__)                                  # no C helpers: the lane moves are plain statements


def _move(tr, lane):
    """The load xmm <- m64 or the store m64 <- xmm, on qword `lane` of the register (1 = high)."""
    tr.need_ops(2)
    if tr.xr(0) is not None and tr.is_mem(1) and tr.bits(1) == 64:          # load, 0F 12 / 0F 16
        return ["uint64_t v = %s(%s);" % (tr.mac(1, "LD64"), tr.ea(1)),     # read first: a fault leaves the register unchanged
                "ctx->xmm[%d].q[%d] = v;" % (tr.xr(0), lane)]
    if tr.is_mem(0) and tr.bits(0) == 64 and tr.xr(1) is not None:          # store, 0F 13 / 0F 17
        return ["%s(%s, ctx->xmm[%d].q[%d]);" % (tr.mac(0, "ST64"), tr.ea(0), tr.xr(1), lane)]
    raise Unsupported(tr.name + " operand form")


@F.insn("movhps", enc(0x0F16, mand=0) | enc(0x0F17, mand=0), vector=True)
def t_movhps(tr, flow):
    return _move(tr, 1)


@F.insn("movlps", enc(0x0F12, mand=0) | enc(0x0F13, mand=0), vector=True)
def t_movlps(tr, flow):
    return _move(tr, 0)


@F.insn("movhpd", enc(0x0F16, mand=0x66) | enc(0x0F17, mand=0x66), vector=True)
def t_movhpd(tr, flow):
    return _move(tr, 1)


@F.insn("movlpd", enc(0x0F12, mand=0x66) | enc(0x0F13, mand=0x66), vector=True)
def t_movlpd(tr, flow):
    return _move(tr, 0)
