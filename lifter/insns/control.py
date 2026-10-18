"""Control flow: jcc, jrcxz, jecxz, jmp, call, ret (docs/DESIGN.md section 6).

Every way out of a function is a macro of harness/runtime.h.  `flow` is the verdict of the
control-flow analysis for this instruction (lifter/discover.py, build_cfg): where a branch goes
and whether that is inside the function.  Far call/jmp (FF /3, FF /5) are not registered, and
the core refuses near branches with a 0x66 prefix.
"""

from ..translate import CC, Family, Unsupported, enc


def c_string(text):
    return '"%s"' % "".join(c if 32 <= ord(c) < 127 and c not in '"\\?' else "\\%03o" % (ord(c) & 0xFF) for c in text)

F = Family(__name__)


@F.insn("ret", enc(0xC3, 0xC2))
def t_ret(tr, flow):
    n = tr.ops[0].imm if tr.ops else 0
    return ["RET(%d);" % n]


# near forms only: FF /3 (far call) and FF /5 (far jmp) load a selector too (review finding 7)
@F.insn("call", enc(0xE8) | enc(0xFF, reg=2))
def t_call(tr, flow):
    tr.need_ops(1)
    ret = tr.ins.address + tr.ins.size
    if flow[0] == "call":
        return ["CALL_DIRECT(%s, 0x%x, 0x%x);" % (tr.fn_name(flow[1]), flow[1], ret)]
    return ["CALL_INDIRECT(%s, 0x%x);" % (tr.val(0, 64), ret)]


@F.insn("jmp", enc(0xE9, 0xEB) | enc(0xFF, reg=4))
def t_jmp(tr, flow):
    tr.need_ops(1)
    k = flow[0]
    if k == "jmp":
        return ["goto L_%x;" % flow[1]]
    if k == "tail":
        return ["TAIL_DIRECT(%s, 0x%x);" % (tr.fn_name(flow[1]), flow[1])]
    if k == "end":
        return ["goto L_end;"]
    if k == "ijmp_tail":
        return ["TAIL_INDIRECT(%s);" % tr.val(0, 64)]
    if k == "ijmp_table":
        lines = ["switch (%s - BASE) {" % tr.val(0, 64)]
        for t in flow[1]:
            lines.append("case 0x%x: goto L_%x;" % (t, t))
        if len(flow) > 2:
            # A table that nothing bounds (discover._open_table_reach): the cases are the entries
            # that could be read, and an address that is none of them is not proved unreachable,
            # so it is no TRAP.  The run stops there with a report: the jump (the address), the
            # function, the table and how much of it was read (these words), and the index as
            # the table load saw it, ix_<rva> (lift.lift_function declares it and sets it at the
            # load), appended in hex.  The stop is the one for an instruction without a
            # translation, which is what the jump is for this index; no feature bit is named,
            # so the runtime's report calls it a gap of the lifter, not a fault of the guest.
            # All of it is written here, in the function, so that runtime.h stays as it is:
            # a change to that header compiles every lifted file of every module again.
            _, entry, table, entries = flow[2]
            what = ("jmp %s computed an address that is none of the %d entries the lifter read from the jump "
                    "table at +0x%x of the function at +0x%x (the table has no bounds check); index 0x"
                    % (tr.ins.op_str, entries, table, entry))
            ix = "ix_%x" % tr.ins.address
            lines.append("default: { char w_[%d] = %s; size_t n_ = %d; int s_ = 60; "
                         "while (s_ > 0 && !((%s >> s_) & 15)) s_ -= 4; "
                         "for (; s_ >= 0; s_ -= 4) w_[n_++] = \"0123456789abcdef\"[(%s >> s_) & 15]; "
                         "w_[n_] = 0; (void)w_; UNTRANSLATED(0x%x, w_, \"\"); }"
                         % (len(what) + 17, c_string(what), len(what), ix, ix, tr.ins.address))
        else:
            lines.append("default: TRAP(0x%x);" % tr.ins.address)
        lines.append("}")
        return lines
    raise Unsupported("jmp %s" % tr.ins.op_str, "indirect-jump")


@F.insn("jrcxz", enc(0xE3))
@F.insn("jecxz", enc(0xE3))
def t_jcc(tr, flow):
    tr.need_ops(1)
    n = tr.name
    if n == "jrcxz":
        cond = "ctx->r[1] == 0"
    elif n == "jecxz":
        cond = "R32(1) == 0"
    else:
        cond = CC[n[1:]]
    if flow[0] == "jcc_end":
        return ["if (%s) goto L_end;" % cond]
    if flow[2]:
        return ["if (%s) goto L_%x;" % (cond, flow[1])]
    return ["if (%s) TAIL_DIRECT(%s, 0x%x);" % (cond, tr.fn_name(flow[1]), flow[1])]


for _i, _cc in enumerate(CC):                 # CC is in condition-code order
    F.register("j" + _cc, t_jcc, enc(0x70 + _i, 0x0F80 + _i))
