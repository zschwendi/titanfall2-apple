#!/usr/bin/env python3
"""Instruction census over a game's x64 DLLs.

Disassembles every range listed in each DLL's unwind table (.pdata) and counts what the
lifter will have to translate: mnemonics, instruction-set groups, lock/rep prefixes,
segment overrides, and the shapes of calls and jumps.

Needs Capstone (use the repo venv). Writes census.json and CENSUS.md.

    .venv/bin/python tools/census/insn_census.py game --out inventory
"""

import argparse
import json
import os
import struct
import sys
from collections import Counter
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE, find_pe_files  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

GROUPS = {
    X.X86_GRP_SSE1: "sse1", X.X86_GRP_SSE2: "sse2", X.X86_GRP_SSE3: "sse3", X.X86_GRP_SSSE3: "ssse3",
    X.X86_GRP_SSE41: "sse4.1", X.X86_GRP_SSE42: "sse4.2", X.X86_GRP_AVX: "avx", X.X86_GRP_AVX2: "avx2",
    X.X86_GRP_FPU: "x87", X.X86_GRP_MMX: "mmx", X.X86_GRP_AES: "aes", X.X86_GRP_PCLMUL: "pclmul",
    X.X86_GRP_BMI: "bmi", X.X86_GRP_BMI2: "bmi2", X.X86_GRP_FMA: "fma", X.X86_GRP_SHA: "sha",
    X.X86_GRP_AVX512: "avx512", X.X86_GRP_CMOV: "cmov",
}
SPECIAL = {"syscall", "cpuid", "rdtsc", "rdtscp", "xgetbv", "pushfq", "popfq", "int3", "int", "ud2", "hlt",
           "pause", "xchg", "cmpxchg", "cmpxchg16b", "xadd", "mfence", "lfence", "sfence", "prefetcht0",
           "prefetchnta", "rdrand", "in", "out", "fxsave", "fxrstor", "stmxcsr", "ldmxcsr", "lahf", "sahf"}


def census(path):
    pe = PE(path)
    name = os.path.basename(path)
    out = {"file": name, "insns": 0, "bytes": 0, "undecoded_bytes": 0, "ranges": 0}
    mn, groups, shapes, special = Counter(), Counter(), Counter(), Counter()
    if pe.machine != 0x8664:
        return None
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return None
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    data = pe.data
    for i in range(size // 12):
        begin, end, _ = struct.unpack_from("<III", data, off + 12 * i)
        boff = pe.rva_to_off(begin)
        if boff is None or end <= begin:
            continue
        out["ranges"] += 1
        code = data[boff:boff + (end - begin)]
        pos = 0
        while pos < len(code):
            last = pos
            for ins in md.disasm(code[pos:], pe.image_base + begin + pos):
                last = (ins.address - pe.image_base - begin) + ins.size
                out["insns"] += 1
                out["bytes"] += ins.size
                m = ins.mnemonic
                mn[m] += 1
                for g in ins.groups:
                    if g in GROUPS:
                        groups[GROUPS[g]] += 1
                pfx = ins.prefix
                if pfx[0] == 0xF0:
                    shapes["lock prefix"] += 1
                    special["lock " + m] += 1
                elif pfx[0] in (0xF2, 0xF3) and m.startswith(("movs", "stos", "cmps", "scas", "lods", "rep")):
                    shapes["rep string op"] += 1
                if m in SPECIAL:
                    special[m] += 1
                ops = ins.operands
                for op in ops:
                    if op.type == X.X86_OP_MEM:
                        if op.mem.segment == X.X86_REG_GS:
                            shapes["gs: memory operand"] += 1
                        elif op.mem.segment == X.X86_REG_FS:
                            shapes["fs: memory operand"] += 1
                        if op.mem.base == X.X86_REG_RIP:
                            shapes["rip-relative operand"] += 1
                if m in ("call", "jmp") and ops:
                    op = ops[0]
                    if op.type == X.X86_OP_IMM:
                        shapes[m + " direct"] += 1
                    elif op.type == X.X86_OP_REG:
                        shapes[m + " register"] += 1
                    elif op.mem.base == X.X86_REG_RIP:
                        shapes[m + " [rip+disp] (import or global pointer)"] += 1
                    elif op.mem.index != 0:
                        shapes[m + " [base+index*scale] (table)"] += 1
                    else:
                        shapes[m + " [reg+disp] (vtable slot)"] += 1
                elif m.startswith("ret"):
                    shapes["ret"] += 1
                elif ins.group(X.X86_GRP_JUMP) and m != "jmp":
                    shapes["conditional jump"] += 1
            if last >= len(code):
                break
            # Capstone stopped: skip one byte and resynchronise.
            out["undecoded_bytes"] += 1
            pos = last + 1
    out.update(mnemonics=dict(mn), groups=dict(groups), shapes=dict(shapes), special=dict(special))
    return out


def merge(results, key):
    total = Counter()
    for r in results:
        total.update(r[key])
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--out", default=".")
    ap.add_argument("--skip", default="d3dcompiler_46.dll", help="comma-separated file names to leave out")
    args = ap.parse_args()
    skip = {s.lower() for s in args.skip.split(",") if s}
    paths = [p for p in find_pe_files(args.root) if os.path.basename(p).lower() not in skip]
    with Pool() as pool:
        results = [r for r in pool.map(census, paths) if r]
    results.sort(key=lambda r: -r["insns"])

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "census.json"), "w") as f:
        json.dump(results, f, indent=1)

    insns = sum(r["insns"] for r in results)
    mn, groups, shapes, special = (merge(results, k) for k in ("mnemonics", "groups", "shapes", "special"))
    L = ["# Instruction census", "",
         "%d files, %d instructions, %d distinct mnemonics, %d undecoded bytes." % (
             len(results), insns, len(mn), sum(r["undecoded_bytes"] for r in results)), "",
         "## Per file", "", "| File | Instructions | Code bytes | Undecoded bytes |", "|---|---|---|---|"]
    L += ["| %s | %d | %d | %d |" % (r["file"], r["insns"], r["bytes"], r["undecoded_bytes"]) for r in results]
    L += ["", "## Instruction-set groups", "", "| Group | Instructions | Share |", "|---|---|---|"]
    L += ["| %s | %d | %.3f%% |" % (g, c, 100 * c / insns) for g, c in groups.most_common()]
    L += ["", "## Call, jump and operand shapes", "", "| Shape | Count |", "|---|---|"]
    L += ["| %s | %d |" % (s, c) for s, c in shapes.most_common()]
    L += ["", "## Special instructions", "", "| Instruction | Count |", "|---|---|"]
    L += ["| %s | %d |" % (s, c) for s, c in special.most_common()]
    L += ["", "## Mnemonics by frequency", "", "| # | Mnemonic | Count | Cumulative share |", "|---|---|---|---|"]
    run = 0
    for i, (m, c) in enumerate(mn.most_common(), 1):
        run += c
        L.append("| %d | %s | %d | %.3f%% |" % (i, m, c, 100 * run / insns))
    with open(os.path.join(args.out, "CENSUS.md"), "w") as f:
        f.write("\n".join(L) + "\n")

    print("%d instructions, %d mnemonics" % (insns, len(mn)))
    for n in (20, 50, 100, 200):
        print("  top %3d mnemonics cover %.3f%%" % (n, 100 * sum(c for _, c in mn.most_common(n)) / insns))


if __name__ == "__main__":
    main()
