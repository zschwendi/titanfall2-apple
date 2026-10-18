#!/usr/bin/env python3
"""Pass 1 of the CPU-feature scout: find every CPU-feature-sensitive instruction.

Sweeps each .pdata range of every x64 image under the game root (the census method, see
tools/census/insn_census.py) and records:

  * cpuid, rdtsc, rdtscp, xgetbv, xsetbv, rdrand, rdseed, in, out, syscall, sysenter,
    hlt, int, stmxcsr, ldmxcsr and the xsave/fxsave family, with the function they sit in
    and whether recursive descent from that function's entry reaches them;
  * AVX, AVX2, AES, SHA, BMI2, PCLMUL (plus BMI1 and FMA) group members, with their function;
  * per-image counts, and mnemonic cross-checks for the VEX/AES/SHA/PCLMUL/BMI2 families.

Writes sites.json (to the scratchpad, never the repo). Other cpu_*.py scripts read it.

    .venv/bin/python tools/scout/cpu_sites.py game --out <scratchpad>/sites.json
"""

import argparse
import json
import os
import sys
from collections import Counter, defaultdict
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import (Dis, descend, function_table, game_images, image_name, load)  # noqa: E402

from capstone import x86_const as X  # noqa: E402

# Instructions that carry CPU-feature or privilege meaning; `pause` and `int3` are only counted.
STORE_MN = {"cpuid", "rdtsc", "rdtscp", "xgetbv", "xsetbv", "rdrand", "rdseed", "in", "out", "ins", "outs",
            "syscall", "sysenter", "sysexit", "hlt", "int", "int1", "stmxcsr", "ldmxcsr",
            "xsave", "xsavec", "xsaveopt", "xsaves", "xrstor", "xrstors", "fxsave", "fxrstor",
            "fxsave64", "fxrstor64", "xsave64", "xrstor64", "xsavec64", "xsaves64", "xrstors64",
            "xsaveopt64"}
COUNT_MN = {"pause", "int3", "lfence", "mfence", "sfence"}

ISA_GRP_NAMES = {X.X86_GRP_AVX: "avx", X.X86_GRP_AVX2: "avx2", X.X86_GRP_AES: "aes", X.X86_GRP_SHA: "sha",
                 X.X86_GRP_BMI2: "bmi2", X.X86_GRP_PCLMUL: "pclmul", X.X86_GRP_BMI: "bmi1",
                 X.X86_GRP_FMA: "fma"}
ISA_REQUESTED = {"avx", "avx2", "aes", "sha", "bmi2", "pclmul"}
NON_VEX_V = {"verr", "verw"}  # system instructions that start with 'v' but are not VEX


def scan(path):
    pe = load(path)
    if pe is None:
        return None
    name = image_name(pe)
    entries, funcs = function_table(pe)
    dis = Dis(pe)
    md = dis.md
    stats = {"image": name, "entries": len(entries), "functions": len(funcs), "insns": 0,
             "undecoded": 0, "mn": Counter(), "isa_grp": Counter(), "isa_requested": 0,
             "isa_any": 0, "isa_funcs": 0, "vex_mn_no_group": Counter(), "aes_sha_pcl_bmi2_mn_no_group": Counter()}
    sites, isa = [], []
    for b, e, parent in entries:
        buf = dis.span(b, e)
        pos = 0
        while pos < len(buf):
            last = pos
            for ci in md.disasm(buf[pos:], b + pos):
                stats["insns"] += 1
                mn = ci.mnemonic
                if mn in STORE_MN or mn in COUNT_MN:
                    stats["mn"][mn] += 1
                if mn in STORE_MN:
                    sites.append({"rva": ci.address, "mn": mn, "text": ci.op_str,
                                  "entry": b, "func": parent})
                gset = {ISA_GRP_NAMES[g] for g in ci.groups if g in ISA_GRP_NAMES}
                if gset:
                    for g in gset:
                        stats["isa_grp"][g] += 1
                    stats["isa_any"] += 1
                    if gset & ISA_REQUESTED:
                        stats["isa_requested"] += 1
                    isa.append({"rva": ci.address, "mn": mn, "grp": sorted(gset), "entry": b, "func": parent})
                elif (mn.startswith("v") and mn not in NON_VEX_V) or mn.startswith(("aes", "sha", "pclmul", "pdep",
                                                                                    "pext", "mulx", "rorx",
                                                                                    "shlx", "shrx", "sarx", "bzhi")):
                    key = "vex" if mn.startswith("v") else "aes_sha_pcl_bmi2"
                    (stats["vex_mn_no_group"] if key == "vex" else stats["aes_sha_pcl_bmi2_mn_no_group"])[mn] += 1
                last = (ci.address - b) + ci.size
            if last >= len(buf):
                break
            pos = last + 1
            stats["undecoded"] += 1
    # Reachability by recursive descent, one descent per function that holds a site.
    by_func = defaultdict(list)
    for s in sites:
        by_func[s["func"]].append(s)
    for func, ss in by_func.items():
        ranges = funcs.get(func, [(func, func + 1)])
        insns, _, unresolved = descend(dis, ranges, [b for b, _ in ranges])
        for s in ss:
            s["reached"] = s["rva"] in insns
            s["unresolved_jumps"] = len(unresolved)
        dis.cache_clear()
    stats["isa_funcs"] = len({i["func"] for i in isa})
    for k in ("mn", "isa_grp", "vex_mn_no_group", "aes_sha_pcl_bmi2_mn_no_group"):
        stats[k] = dict(stats[k])
    return {"stats": stats, "sites": sites, "isa": isa}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="game directory")
    ap.add_argument("--out", required=True, help="where to write sites.json (scratchpad)")
    args = ap.parse_args()
    paths = game_images(args.root)
    with Pool(3) as pool:
        results = [r for r in pool.map(scan, paths) if r]
    out = {"images": [], "sites": [], "isa": []}
    for r in sorted(results, key=lambda r: r["stats"]["image"]):
        st = r["stats"]
        out["images"].append(st)
        out["sites"].extend(dict(s, img=st["image"]) for s in r["sites"])
        out["isa"].extend(dict(s, img=st["image"]) for s in r["isa"])
    with open(args.out, "w") as f:
        json.dump(out, f)

    print("%-26s %9s %8s %6s %9s %9s %9s" % ("image", "insns", "entries", "undec", "isa_any", "isa_req",
                                              "isa_funcs"))
    for st in out["images"]:
        print("%-26s %9d %8d %6d %9d %9d %9d" % (st["image"], st["insns"], st["entries"], st["undecoded"],
                                                 st["isa_any"], st["isa_requested"], st["isa_funcs"]))
    tot = Counter()
    for st in out["images"]:
        tot.update(st["mn"])
    print("\nmnemonics (all images):", dict(sorted(tot.items())))
    isa_tot = Counter()
    for st in out["images"]:
        isa_tot.update(st["isa_grp"])
    print("isa group membership (sum over images):", dict(sorted(isa_tot.items())))
    print("isa requested union (any of avx avx2 aes sha bmi2 pclmul):",
          sum(st["isa_requested"] for st in out["images"]))
    print("isa per image:")
    for st in out["images"]:
        if st["isa_any"]:
            print("  %-26s %s" % (st["image"], dict(sorted(st["isa_grp"].items()))))
    nog = Counter()
    for st in out["images"]:
        nog.update(st["vex_mn_no_group"])
        nog.update(st["aes_sha_pcl_bmi2_mn_no_group"])
    print("mnemonics that look like ISA extensions but carry no group:", dict(nog.most_common(20)) or "none")
    rn = Counter((s["mn"], s.get("reached")) for s in out["sites"])
    print("\nsites by mnemonic and recursive-descent reachability:")
    for k in sorted(rn, key=lambda k: (k[0], str(k[1]))):
        print("  %-10s reached=%-5s %d" % (k[0], k[1], rn[k]))


if __name__ == "__main__":
    main()
