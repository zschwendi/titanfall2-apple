#!/usr/bin/env python3
"""Slots of a vtable, each with what its function refers to (finding 37).

    .venv/bin/python tools/scout/flow_vtable.py MODULE CLASS_REGEX          # list matching RTTI vtables
    .venv/bin/python tools/scout/flow_vtable.py MODULE 0xVTABLE [FIRST [LAST]]   # slots FIRST..LAST

For each slot: the function's rva, its size in bytes, the imports it calls, how many direct and
virtual calls it makes, and the first strings it loads.  A slot that is a tiny stub is shown with
its instruction count.  Prints addresses, names and counts.  OriginSDK.dll is refused.
"""

import re
import sys

from flow_lib import get, fmt_imp
from capstone import x86_const as X


def summary(M, f, nstr=3):
    imps, direct, virt, strs = [], 0, 0, []
    ins_list = M.insns(f)
    for ins in ins_list:
        if ins.mnemonic in ("call", "jmp"):
            ct = M.call_target(ins)
            if not ct:
                continue
            if ct[0] == "import":
                n = fmt_imp(ct[1])
                if n not in imps:
                    imps.append(n)
            elif ct[0] == "direct" and (ins.mnemonic == "call" or M.func_of(ct[1]) != M.func_of(ins.address)):
                direct += 1
            elif ct[0] in ("vslot", "reg", "gptr"):
                virt += 1
        elif ins.mnemonic == "lea":
            for op in ins.operands:
                if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                    s = M.string_at(ins.address + ins.size + op.mem.disp)
                    if s and len(strs) < nstr and s not in strs:
                        strs.append(s[:60])
    return len(ins_list), imps, direct, virt, strs


def main():
    M = get(sys.argv[1])
    arg = sys.argv[2]
    if not arg.startswith("0x"):
        rx = re.compile(arg)
        for rva, c in sorted(M.vtables().items()):
            if rx.search(c):
                print("%s+0x%x  %-50s %d slots" % (M.short, rva, c, len(M.vtable_slots(rva))))
        return
    vt = int(arg, 16)
    slots = M.vtable_slots(vt)
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    last = int(sys.argv[4]) if len(sys.argv) > 4 else len(slots) - 1
    print("%s+0x%x  %s  %d slots" % (M.short, vt, M.vtable_class(vt) or "(no RTTI)", len(slots)))
    for i in range(first, min(last, len(slots) - 1) + 1):
        f = slots[i]
        n, imps, direct, virt, strs = summary(M, f)
        print("  %3d  +0x%-6x %4d insn  %2d direct %2d indirect  %s  %s" % (
            i, f, n, direct, virt, " ".join(imps[:8]), " | ".join(repr(s) for s in strs)))


if __name__ == "__main__":
    main()
