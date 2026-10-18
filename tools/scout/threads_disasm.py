#!/usr/bin/env python3
"""Annotated disassembly viewer for the thread scouting work (issue #29). Terminal use only.

    .venv/bin/python tools/scout/threads_disasm.py MODULE START_RVA [MAX_INSNS] [--end RVA]

Annotates: import calls (`call [slot]` shows dll!name), rip-relative string loads, direct
call targets (with export names when known), and rip-relative data references. Not meant to
be pasted into any file; the output is for reading.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402


def annotate(m, ins, slots, strs, exp):
    notes = []
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
            tgt = ins.address + ins.size + op.mem.disp
            if tgt in slots:
                notes.append("IMPORT %s!%s" % slots[tgt])
            elif tgt in strs:
                notes.append("str %r" % strs[tgt][:70])
            else:
                notes.append("data %s+0x%x" % (m.sec_name(tgt) or "?", tgt))
        elif op.type == X.X86_OP_IMM and ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            tgt = op.imm
            if tgt in exp:
                notes.append("-> %s" % exp[tgt][:60])
            else:
                notes.append("-> f%x" % tgt)
    return "   ; " + "; ".join(notes) if notes else ""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    end = None
    if "--end" in sys.argv:
        end = int(sys.argv[sys.argv.index("--end") + 1], 16)
        args = [a for a in args if a != sys.argv[sys.argv.index("--end") + 1]]
    m = Module(args[0])
    start = int(args[1], 16)
    maxn = int(args[2]) if len(args) > 2 else 120
    slots = m.import_slots()
    strs = m.strings()
    exp = {r: n for n, o, r in m.exports()}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    fe = m.func_end(start)
    if end is None:
        end = fe if fe else start + 0x400
    code = m.bytes_at(start, end - start)
    n = 0
    for ins in md.disasm(code, start):
        print("%7x  %-7s %-40s%s" % (ins.address, ins.mnemonic, ins.op_str, annotate(m, ins, slots, strs, exp)))
        n += 1
        if n >= maxn:
            break


if __name__ == "__main__":
    main()
