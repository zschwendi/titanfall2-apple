#!/usr/bin/env python3
"""Outline of one function for reading a start-up sequence at the terminal (finding 37).

    .venv/bin/python tools/scout/flow_show.py MODULE FUNC_RVA [--all] [--from RVA] [--to RVA]

Default output is an outline: one line per call (import name, export name, virtual slot or
target rva), per string the code loads, per code or vtable address taken, per return, and the
conditional branches as `jcc -> rva`.  With --all every instruction is printed (use --from/--to
to keep it short).  Terminal use only: the output holds instruction text of the game and is
not stored or committed.  OriginSDK.dll is refused.
"""

import sys

from flow_lib import get, fmt_imp
from capstone import x86_const as X


def notes(M, ins):
    out = []
    if ins.mnemonic in ("call", "jmp"):
        ct = M.call_target(ins)
        if ct:
            k, v = ct
            if k == "import":
                out.append("IMPORT " + fmt_imp(v))
            elif k == "direct":
                if ins.mnemonic == "call" or M.func_of(v) != M.func_of(ins.address):
                    out.append("-> " + M.label(v))
            elif k == "vslot":
                out.append("VCALL [%s+0x%x] slot %d" % (v[0], v[1], v[1] // 8))
            elif k == "gptr":
                out.append("GPTR %s+0x%x" % (M.short, v))
            elif k == "reg":
                out.append("REG " + v)
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP and ins.mnemonic not in ("call", "jmp"):
            t = ins.address + ins.size + op.mem.disp
            if t in M.slots:
                out.append("slot " + fmt_imp(M.slots[t]))
                continue
            s = M.string_at(t) if ins.mnemonic == "lea" else None
            if s is not None:
                out.append("str %r" % s[:90])
            elif ins.mnemonic == "lea" and M.is_code(t):
                out.append("code " + M.label(t))
            elif ins.mnemonic == "lea" and M.vtable_class(t):
                out.append("vtable " + M.vtable_class(t))
            else:
                out.append("data %s+0x%x" % (M.m.sec_name(t) or "?", t))
    return out


def main():
    a = sys.argv[1:]
    full = "--all" in a
    lo = int(a[a.index("--from") + 1], 16) if "--from" in a else 0
    hi = int(a[a.index("--to") + 1], 16) if "--to" in a else 1 << 62
    pos = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or a[i - 1] not in ("--from", "--to"))]
    M = get(pos[0])
    f = int(pos[1], 16)
    print("%s+0x%x  %s  %d bytes in %d piece(s)" % (M.short, f, M.exports.get(f, ""), M.size(f), len(M.pieces(f))))
    for ins in M.insns(f):
        if not (lo <= ins.address < hi):
            continue
        n = notes(M, ins)
        if full:
            print("%7x  %-7s %-38s%s" % (ins.address, ins.mnemonic, ins.op_str, ("  ; " + "; ".join(n)) if n else ""))
            continue
        if ins.mnemonic == "call" or (ins.mnemonic == "jmp" and n):
            print("%7x  %-5s %s" % (ins.address, ins.mnemonic, "; ".join(n) or ins.op_str))
        elif ins.mnemonic.startswith("j") and ins.mnemonic != "jmp":
            print("%7x    %s -> %s" % (ins.address, ins.mnemonic, ins.op_str))
        elif ins.mnemonic.startswith("ret"):
            print("%7x  ret" % ins.address)
        elif any(x.startswith(("str ", "code ", "vtable ", "slot ")) for x in n):
            print("%7x    %s %s" % (ins.address, ins.mnemonic, "; ".join(x for x in n if not x.startswith("data "))))


if __name__ == "__main__":
    main()
