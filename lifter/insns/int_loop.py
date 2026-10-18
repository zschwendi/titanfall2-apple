"""Loops: loop, loopne (docs/DESIGN.md section 6).

Both count RCX down by one (all 64 bits, no flag changes) and jump on the result, as jrcxz does;
loopne also needs ZF clear and reads it. Every way out of a function is a macro of
harness/runtime.h. `flow` is the verdict of the control-flow analysis for this instruction
(lifter/discover.py, build_cfg): it treats them as conditional jumps. An address-size prefix
makes the counter ECX; that form is refused.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("loop", enc(0xE2, mand=0))
@F.insn("loopne", enc(0xE0, mand=0))
def t_loop(tr, flow):
    tr.need_ops(1)
    if tr.ins.addr_size != 8:
        raise Unsupported("loop with a 32-bit counter")
    if tr.name == "loopne":
        cond = "ctx->r[1] != 0 && !ctx->zf"
    else:
        cond = "ctx->r[1] != 0"
    dec = "ctx->r[1] -= 1;"
    if flow[0] == "jcc_end":
        return [dec, "if (%s) goto L_end;" % cond]
    if flow[2]:
        return [dec, "if (%s) goto L_%x;" % (cond, flow[1])]
    return [dec, "if (%s) TAIL_DIRECT(%s, 0x%x);" % (cond, tr.fn_name(flow[1]), flow[1])]
