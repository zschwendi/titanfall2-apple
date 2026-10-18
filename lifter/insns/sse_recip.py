"""SSE approximate reciprocal and reciprocal square root: rcpss, rcpps, rsqrtss, rsqrtps.

These are not `1.0f / x` and `1.0f / sqrtf(x)`.  A real CPU looks the result up in a table: 12
bits of result, the rest of the mantissa zero, relative error at most 1.5 * 2^-12 (SDM, RCPSS).
The table is the vendor's.  The one reproduced here is Intel's; an AMD processor returns other
bits for the same operand.

The translation is one host instruction per lane, frecpe or frsqrte, written out by tf_frecpe32
and tf_frsqrte32 of harness/runtime.h.  In the guest float mode (FPCR.AH, harness/fpmode.h) that
instruction is the Intel function bit for bit: every one of the 2^32 operands under all 16 MXCSR
settings (tools/hworacle/sweep.py), and through these translators 97,236 cases, 256,200 result
lanes, of every table entry and every special operand under the seven MXCSR settings of the
float test (harness/recip_sweep.py; docs/handoff/lifter-recip.md): 0 differences.  So no class
needs a correction.  What the CPU does, and the host instruction with it: a zero gives an
infinity of its sign and an infinity a zero; a NaN comes back quieted; a denormal operand gives
an infinity of its sign, with or without DAZ (for rsqrt too, a negative one included); any other
negative operand of rsqrt gives the default NaN ffc00000; rcp of an operand of 2^126 or more,
whose result would be denormal, gives a zero.  No result depends on the MXCSR setting.

The emulator divides exactly, so it is wrong for all four and never the expected side: these
mnemonics are judged by the real CPU only (harness/oraclefix.py, HARDWARE_ONLY).

Scalar forms: lane 0 from the source's lane 0 (an xmm register or an m32); the upper lanes of the
destination are unchanged.  Packed forms: four lanes from an xmm register or an aligned m128
(tr.xval emits the alignment fault), read whole before anything is written.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)

_HELPER = {"rcp": "tf_frecpe32", "rsqrt": "tf_frsqrte32"}


@F.insn("rcpss", enc(0x0F53, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("rsqrtss", enc(0x0F52, mand=0xF3), vector=True, fp="f32 scalar")
def t_recip_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    return ["float s = %s;" % tr.fval(1, 32),                      # the source is read before anything is written
            "ctx->xmm[%d].f[0] = %s(s);" % (d, _HELPER[tr.name[:-2]])]


@F.insn("rcpps", enc(0x0F53, mand=0), vector=True, fp="f32 packed")
@F.insn("rsqrtps", enc(0x0F52, mand=0), vector=True, fp="f32 packed")
def t_recip_packed(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    out = ["Xmm s = %s;" % tr.xval(1)]                             # an xmm register or an aligned m128, read whole first
    for j in range(4):
        out.append("ctx->xmm[%d].f[%d] = %s(s.f[%d]);" % (d, j, _HELPER[tr.name[:-2]], j))
    return out
