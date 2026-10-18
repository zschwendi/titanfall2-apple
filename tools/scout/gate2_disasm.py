#!/usr/bin/env python3
"""Annotated disassembly of one function of tier0.dll or vstdlib.dll, for reading at the terminal.

    .venv/bin/python tools/scout/gate2_disasm.py tier0 0x2a388 [max_insns]

Calls are annotated with the import name, the export name, or the lifter status of the target;
RIP-relative operands with the section and (for short ASCII strings) the string.  Output is for the
analyst's screen; none of it is stored by the scripts.  It decodes straight-line from the start
address through unconditional jumps and stops at `ret`/`int3` once past the last branch target seen.
"""

import re
import sys

import gate2_lib as L

RIP = re.compile(r"\[rip ([+-]) (0x[0-9a-f]+)\]")


def annotate(M, max_insns=400, start=None):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    exp_by_rva = {}
    for e in M.exports:
        exp_by_rva.setdefault(e["rva"], e["name"])
    pe = M.pe
    out = []
    pc = start
    far = start
    n = 0
    while n < max_insns:
        off = pe.rva_to_off(pc)
        if off is None:
            break
        ins = next(md.disasm(pe.data[off:off + 16], pc, 1), None)
        if ins is None:
            out.append((pc, "db", "?", ""))
            pc += 1
            n += 1
            continue
        note = ""
        op = ins.op_str
        m = RIP.search(op)
        if m:
            d = int(m.group(2), 16)
            tgt = ins.address + ins.size + (d if m.group(1) == "+" else -d)
            sec = M.m.section_name(tgt)
            s = M.slots.get(tgt)
            if s:
                note = "import %s!%s" % (s[0], s[1])
            elif sec in (".data", ".tls"):
                note = "%s+%#x" % (sec, tgt)
            else:
                st = M.m.any_string(tgt) if sec not in (None,) else None
                note = "%s+%#x %s" % (sec, tgt, ("'%s'" % st[:60]) if st else "")
        if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
            if op.startswith("0x"):
                t = int(op, 16)
                if t in M.thunks:
                    note = "import %s!%s" % M.thunks[t]
                else:
                    u = M.m.unit_of(t)
                    nm = exp_by_rva.get(t)
                    st = M.status(t) if t in M.rfun else "?"
                    note = "fn %#x%s [%s]" % (t, (" export " + nm) if nm else "", st)
                far = max(far, t) if ins.mnemonic != "call" and t > ins.address else far
        out.append((ins.address, ins.mnemonic, op, note))
        n += 1
        pc = ins.address + ins.size
        if ins.mnemonic in ("ret", "int3") and pc > far:
            break
        if ins.mnemonic == "jmp" and pc > far:
            break
    return out


def main():
    name = sys.argv[1]
    rva = int(sys.argv[2], 16)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 400
    M = L.load(name)
    for a, mn, op, note in annotate(M, n, rva):
        print("%#8x  %-7s %-44s %s" % (a, mn, op, note))


if __name__ == "__main__":
    main()
