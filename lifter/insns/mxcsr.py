"""MXCSR: stmxcsr, ldmxcsr.

ctx->mxcsr holds the guest's MXCSR.  ldmxcsr stores the new value and re-enters the guest float
mode (harness/fpmode.h), so that the rounding mode, DAZ and FTZ it selects take effect for the
float instructions that follow.  The six status flags are stored and returned as they were
loaded; float instructions do not set them (not modelled: docs/DESIGN.md section 4).

Bits 16-31 are reserved: loading a value that sets one is #GP(0) on x86 (SDM, LDMXCSR), a trap
here.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("stmxcsr", enc(0x0FAE, reg=3, mand=0))
def t_stmxcsr(tr, flow):
    tr.need_ops(1)
    if not tr.is_mem(0) or tr.bits(0) != 32:
        raise Unsupported("stmxcsr operand")
    return [tr.wr(0, "ctx->mxcsr", 32)]


@F.insn("ldmxcsr", enc(0x0FAE, reg=2, mand=0))
def t_ldmxcsr(tr, flow):
    tr.need_ops(1)
    if not tr.is_mem(0) or tr.bits(0) != 32:
        raise Unsupported("ldmxcsr operand")
    return ["uint32_t v = %s;" % tr.val(0, 32),
            "if (v & UINT32_C(0xFFFF0000)) TRAP(0x%x);" % tr.ins.address,
            "ctx->mxcsr = v;",
            "tf_fp_enter_guest(v);"]
