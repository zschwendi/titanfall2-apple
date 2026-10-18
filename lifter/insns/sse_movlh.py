"""SSE moves between the two halves of an xmm register: movlhps (0F 16 /r) and movhlps (0F 12 /r),
register source only. The memory forms of the same opcodes are movhps and movlps, other tasks'
mnemonics, so they never reach these translators.

Each one is a quadword shuffle of the two whole registers, done by one host vector instruction
(harness/runtime.h, Lane shuffles of whole registers: TF_SHUF2, lanes chosen at translation time):
movlhps keeps the destination's low quadword and takes the source's low quadword as the high one,
TF_SHUF2(d, s, 0, 2); movhlps takes the source's high quadword as the low one and keeps the
destination's high quadword, TF_SHUF2(d, s, 3, 1).  The source is copied into a local before the
destination is written (the source may be the destination register).
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


def _xmm_xmm(tr):
    """Destination and source xmm numbers of the xmm, xmm form; refuses any other form."""
    tr.need_ops(2)
    d = tr.xr(0)
    s = tr.xr(1)
    if d is None or s is None:
        raise Unsupported(tr.name + " operand")
    return d, s


@F.insn("movlhps", enc(0x0F16, mand=0), vector=True)
def t_movlhps(tr, flow):
    d, s = _xmm_xmm(tr)
    return ["Xmm s = ctx->xmm[%d];" % s,          # read the source before writing anything
            "ctx->xmm[%d] = TF_SHUF2(ctx->xmm[%d], s, 0, 2);" % (d, d)]


@F.insn("movhlps", enc(0x0F12, mand=0), vector=True)
def t_movhlps(tr, flow):
    d, s = _xmm_xmm(tr)
    return ["Xmm s = ctx->xmm[%d];" % s,
            "ctx->xmm[%d] = TF_SHUF2(ctx->xmm[%d], s, 3, 1);" % (d, d)]
