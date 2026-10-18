#!/usr/bin/env python3
"""Recover ConVar registrations (name, default string, flags, help) and the places that read them (issue #29).

The engine registers each ConVar from a small static-init function:
    lea rdx,[name]  lea rcx,[convar object]  lea r8,[default string]  mov r9d,flags  mov [rsp+20h],help  call ctor
This script finds, for each requested name, that function, decodes those arguments, then (with
--reads) scans the module for code that touches the ConVar object (rip-relative) and prints the
functions doing so.

    .venv/bin/python tools/scout/threads_convars.py MODULE [--reads] NAME [NAME ...]
"""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402
import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402


def cstr(m, rva):
    b = m.bytes_at(rva, 120)
    e = b.find(b"\0")
    return b[:e].decode("latin-1") if e >= 0 else b.decode("latin-1")


def main():
    args = sys.argv[1:]
    reads = "--reads" in args
    args = [a for a in args if a != "--reads"]
    m = Module(args[0])
    names = set(args[1:])
    strs = m.strings()
    name_rva = {}
    for rva, s in strs.items():
        if s in names:
            name_rva.setdefault(s, []).append(rva)
    sites = defaultdict(list)
    for site, tgt in m.lea_refs():
        for n, rvas in name_rva.items():
            if tgt in rvas:
                sites[n].append(site)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    objs = {}
    for n in sorted(sites):
        for site in sites[n]:
            ent = m.entry_of(site)
            if not ent:
                continue
            code = m.bytes_at(ent[0], ent[1] - ent[0])
            insns = list(md.disasm(code, ent[0]))
            i = next((k for k, x in enumerate(insns) if x.address == site), None)
            if i is None:
                continue
            regs = {}
            j = i
            while j < len(insns) and insns[j].mnemonic != "call":
                j += 1
            win = insns[max(0, i - 8):j + 1]
            for x in win:
                if x.mnemonic == "lea" and x.operands[1].type == X.X86_OP_MEM and x.operands[1].mem.base == X.X86_REG_RIP:
                    t = x.address + x.size + x.operands[1].mem.disp
                    regs[x.reg_name(x.operands[0].reg)] = t
                elif x.mnemonic == "mov" and x.operands[0].type == X.X86_OP_REG and x.operands[1].type == X.X86_OP_IMM:
                    regs[x.reg_name(x.operands[0].reg)] = ("imm", x.operands[1].imm)
                elif x.mnemonic == "mov" and x.operands[0].type == X.X86_OP_MEM and x.operands[0].mem.base == X.X86_REG_RSP:
                    regs["[rsp+%x]" % x.operands[0].mem.disp] = (x.op_str.split(",", 1)[1].strip())
            obj = regs.get("rcx")
            dflt = regs.get("r8")
            flags = regs.get("r9d") or regs.get("r9")
            helpr = regs.get("rax") or regs.get("[rsp+20]") or regs.get("[rsp+0x20]")
            ds = cstr(m, dflt) if isinstance(dflt, int) else dflt
            hs = cstr(m, helpr) if isinstance(helpr, int) else helpr
            print("%-34s site %x in f%x  obj .data+0x%x default %r flags %s help %r" % (
                n, site, ent[2], obj if isinstance(obj, int) else 0, ds, flags[1] if isinstance(flags, tuple) else flags, (hs or "")[:60]))
            if isinstance(obj, int):
                objs[n] = obj
    if reads and objs:
        want = {}
        for n, o in objs.items():
            want[n] = o
        hits = defaultdict(set)
        # full disassembly per function
        if m._funcs is None:
            m._load_funcs()
        for (b, e, parent) in m._funcs:
            code = m.bytes_at(b, e - b)
            for x in md.disasm(code, b):
                for op in x.operands:
                    if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                        t = x.address + x.size + op.mem.disp
                        for n, o in want.items():
                            if o <= t < o + 0x60:
                                hits[n].add((parent, t - o))
        for n in sorted(hits):
            fs = sorted(hits[n])
            print("READS/WRITES of %s obj: %s" % (n, " ".join("f%x(+%x)" % (f, d) for f, d in fs[:20])))


if __name__ == "__main__":
    main()
