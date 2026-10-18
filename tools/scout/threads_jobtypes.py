#!/usr/bin/env python3
"""Job types registered with tier0's JT system, with the thread-affinity masks (issue #29).

For each call to JTGuts_RegisterJobType / JT_RegisterCallJobType, report the module, enclosing
function, the nearest preceding string (the job name) and, if the next JT_SetJobTypeAffinity call
sets constant masks, those masks (second arg = threads that run the type, third = threads that
may help with it; bit 0 = main thread, bit 1 = render thread slot, bits 2.. = workers).

    .venv/bin/python tools/scout/threads_jobtypes.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, SOURCE_MODULES  # noqa: E402
import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402


def main():
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    rows = []
    for name in SOURCE_MODULES:
        if name == "tier0.dll":
            continue
        m = Module(name)
        slots = m.import_slots()
        reg = {s for s, (d, n) in slots.items() if d == "tier0.dll" and ("JTGuts_RegisterJobType" in n or "JT_RegisterCallJobType" in n)}
        aff = {s for s, (d, n) in slots.items() if d == "tier0.dll" and "JT_SetJobTypeAffinity" in n}
        if not reg:
            continue
        strs = m.strings()
        sites = m.iat_refs(reg | aff)
        by_func = {}
        for site, kind, slot in sites:
            ent = m.entry_of(site)
            if ent:
                by_func.setdefault(ent[0], []).append((site, "reg" if slot in reg else "aff"))
        for fb, lst in by_func.items():
            ent = m.entry_of(fb)
            insns = list(md.disasm(m.bytes_at(ent[0], ent[1] - ent[0]), ent[0]))
            idx = {x.address: i for i, x in enumerate(insns)}
            for k, (site, kind) in enumerate(lst):
                if kind != "reg":
                    continue
                i = idx[site]
                nm = "?"
                for x in reversed(insns[max(0, i - 30):i]):
                    if x.mnemonic == "lea" and x.operands[1].type == X.X86_OP_MEM and x.operands[1].mem.base == X.X86_REG_RIP:
                        t = x.address + x.size + x.operands[1].mem.disp
                        if t in strs and len(strs[t]) < 40:
                            nm = strs[t]
                            break
                masks = "-"
                for site2, kind2 in lst[k + 1:]:
                    if kind2 == "aff" and site2 - site < 0x80:
                        j = idx[site2]
                        val = {}
                        for x in reversed(insns[max(0, j - 8):j]):
                            if x.mnemonic == "mov" and x.operands[0].type == X.X86_OP_REG and x.operands[1].type == X.X86_OP_IMM:
                                r = x.reg_name(x.operands[0].reg)
                                val.setdefault(r, x.operands[1].imm & 0xffffffff)
                            elif x.mnemonic == "mov" and x.operands[0].type == X.X86_OP_REG and x.operands[1].type == X.X86_OP_REG:
                                a, b = x.reg_name(x.operands[0].reg), x.reg_name(x.operands[1].reg)
                                if b in val and a not in val:
                                    val[a] = val[b]
                                elif a not in val:
                                    val.setdefault("alias:" + a, b)
                        a_, b_ = val.get("edx"), val.get("r8d")
                        if b_ is None and val.get("alias:r8d") == "edx":
                            b_ = a_
                        masks = "run=0x%x help=0x%x" % (a_, b_) if a_ is not None and b_ is not None else "non-constant"
                        break
                rows.append((name, ent[2], nm, masks))
    seen = set()
    for r in rows:
        if r in seen:
            continue
        seen.add(r)
        print("%-24s f%-8x %-34s %s" % r)


if __name__ == "__main__":
    main()
