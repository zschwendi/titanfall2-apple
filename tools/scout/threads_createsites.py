#!/usr/bin/env python3
"""Describe every CreateThread call site: enclosing function, start routine, strings (issue #29).

For each site recorded by threads_scan.py (CreateThread / QueueUserAPC / _beginthreadex) this
walks backwards over the instructions before the call and reports what was loaded into
rcx (security), rdx (stack size), r8 (start routine), r9 (parameter) and the two stack
arguments (creation flags, id pointer), plus the strings, imports and direct calls inside
the enclosing function. Argument values are the last write seen in the 24 instructions before
the call, so a register that is set far earlier may show a stale value (check by hand).

    .venv/bin/python tools/scout/threads_createsites.py CACHE_DIR [module ...]
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, SOURCE_MODULES, THIRD_PARTY  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

ARGREG = {X.X86_REG_RCX: "rcx", X.X86_REG_ECX: "rcx", X.X86_REG_RDX: "rdx", X.X86_REG_EDX: "rdx",
          X.X86_REG_R8: "r8", X.X86_REG_R8D: "r8", X.X86_REG_R9: "r9", X.X86_REG_R9D: "r9"}


def describe(m, site, func, md, slots, strs, exp):
    ent = m.entry_of(site)
    start = ent[0] if ent else func
    code = m.bytes_at(start, (ent[1] if ent else m.func_end(func)) - start)
    insns = list(md.disasm(code, start))
    idx = None
    for i, ins in enumerate(insns):
        if ins.address == site:
            idx = i
            break
    if idx is None:
        return None
    args = {}
    stackargs = {}
    for ins in reversed(insns[max(0, idx - 24):idx]):
        if ins.mnemonic in ("call",):
            break
        ops = ins.operands
        if ins.mnemonic in ("mov", "lea", "xor", "movzx") and ops and ops[0].type == X.X86_OP_REG and ops[0].reg in ARGREG:
            r = ARGREG[ops[0].reg]
            if r in args:
                continue
            if ins.mnemonic == "lea" and ops[1].type == X.X86_OP_MEM and ops[1].mem.base == X.X86_REG_RIP:
                tgt = ins.address + ins.size + ops[1].mem.disp
                s = m.sec_name(tgt)
                args[r] = "lea %s+0x%x (%s)" % (s, tgt, "code" if s == ".text" else "data")
            elif ins.mnemonic == "xor":
                args[r] = "0"
            else:
                args[r] = "%s %s" % (ins.mnemonic, ins.op_str)
        if ins.mnemonic == "mov" and ops and ops[0].type == X.X86_OP_MEM and ops[0].mem.base == X.X86_REG_RSP:
            d = ops[0].mem.disp
            if d in (0x20, 0x28) and d not in stackargs:
                stackargs[d] = ins.op_str.split(",", 1)[1].strip()
    # strings, imports and calls inside the function
    sstr, simp, scalls = [], set(), []
    for ins in insns:
        for op in ins.operands:
            if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                tgt = ins.address + ins.size + op.mem.disp
                if tgt in strs and ins.mnemonic == "lea":
                    sstr.append(strs[tgt][:50])
                elif tgt in slots:
                    simp.add(slots[tgt][1])
            elif op.type == X.X86_OP_IMM and ins.mnemonic == "call":
                scalls.append(op.imm)
    names = [exp.get(c) for c in scalls if exp.get(c)]
    return args, stackargs, sstr[:6], sorted(simp), names[:6], len(insns)


def main():
    cache = sys.argv[1]
    names = sys.argv[2:] or (SOURCE_MODULES + THIRD_PARTY)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for name in names:
        p = os.path.join(cache, name + ".scan.json")
        d = json.load(open(p))
        sites = [s for s in d["sites"] if s["api"] in ("CreateThread", "_beginthreadex", "QueueUserAPC")
                 and s["kind"] in ("call", "call-thunk") and s["func"] is not None]
        if not sites:
            continue
        m = Module(name)
        slots = m.import_slots()
        strs = m.strings()
        exp = {r: n for n, o, r in m.exports()}
        print("== %s" % name)
        for s in sites:
            r = describe(m, s["site"], s["func"], md, slots, strs, exp)
            if r is None:
                print("  site %x func %x: could not decode" % (s["site"], s["func"]))
                continue
            args, stackargs, sstr, simp, calls, n = r
            ex = [nm for nm, o, rv in m.exports() if rv == s["func"]]
            print("  site %x in f%x%s (%d insns) %s" % (s["site"], s["func"], " = export " + ex[0] if ex else "", n, s["api"]))
            print("     args:", args, "stack:", stackargs)
            print("     strings:", sstr)
            print("     imports:", simp)
            print("     export-calls:", calls)


if __name__ == "__main__":
    main()
