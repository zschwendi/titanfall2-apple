#!/usr/bin/env python3
"""Count memory-ordering-relevant instructions per module (issue #29): lock-prefixed
instructions, xchg with a memory operand (implicitly locked), fences and pause.

Walks every function in .pdata, so it counts code, not data. Up to 4 worker processes.

    .venv/bin/python tools/scout/threads_atomics.py [module ...]
"""
import multiprocessing as mp
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, SOURCE_MODULES  # noqa: E402
import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402


def work(name):
    m = Module(name)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    c = Counter()
    ops = Counter()
    m._load_funcs()
    for b, e, parent in m._funcs:
        if parent != b:
            pass
        for ins in md.disasm(m.bytes_at(b, e - b), b):
            if ins.prefix[0] == 0xF0:
                c["lock"] += 1
                ops[ins.mnemonic] += 1
            elif ins.mnemonic == "xchg" and any(o.type == X.X86_OP_MEM for o in ins.operands):
                c["xchg_mem"] += 1
            elif ins.mnemonic in ("mfence", "sfence", "lfence"):
                c[ins.mnemonic] += 1
            elif ins.mnemonic == "pause":
                c["pause"] += 1
    return name, c, ops


def main():
    names = sys.argv[1:] or SOURCE_MODULES
    tot = Counter()
    allops = Counter()
    with mp.Pool(4) as pool:
        res = pool.map(work, names)
    print("| Module | lock-prefixed | xchg [mem] | fences | pause |")
    print("|---|---|---|---|---|")
    for name, c, ops in res:
        print("| %s | %d | %d | %d | %d |" % (name, c["lock"], c["xchg_mem"], c["mfence"] + c["sfence"] + c["lfence"], c["pause"]))
        tot.update(c)
        allops.update(ops)
    print("| **total** | %d | %d | %d | %d |" % (tot["lock"], tot["xchg_mem"], tot["mfence"] + tot["sfence"] + tot["lfence"], tot["pause"]))
    print()
    print("lock-prefixed mnemonics:", ", ".join("%s %d" % kv for kv in allops.most_common()))


if __name__ == "__main__":
    main()
