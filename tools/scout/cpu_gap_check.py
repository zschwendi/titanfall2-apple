#!/usr/bin/env python3
"""Byte-pattern hits for cpuid (0F A2) and rdtsc (0F 31) that the .pdata sweep did not decode.

For every raw hit, classify it as:
  decoded      an instruction of that mnemonic starts there (linear decode of its region);
  boundary     an instruction starts there, with another mnemonic (byte pair inside an operand);
  not_a_start  no instruction starts at that byte (the pair sits inside another instruction).
Hits are split into inside .pdata ranges and in gaps (executable bytes with no .pdata range).

    .venv/bin/python tools/scout/cpu_gap_check.py game
"""

import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import Dis, descend, function_table, game_images, load, preds  # noqa: E402
from cpu_q1_cpuid import ConstEval  # noqa: E402
from cpu_q1_cpuid import forward_uses, single  # noqa: E402
from cpu_q3_q4 import tsc_purpose, walk  # noqa: E402

PATTERNS = {"cpuid": (re.compile(rb"\x0f\xa2"), "cpuid"), "rdtsc": (re.compile(rb"\x0f\x31"), "rdtsc")}


def region_for(a, cover):
    for b, e in cover:
        if b <= a < e:
            return b, e, True
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    paths = game_images(sys.argv[1])
    summary = collections.Counter()
    detail = []
    for path in paths:
        pe = load(path)
        entries, _ = function_table(pe)
        cover = sorted((b, e) for b, e, _ in entries)
        dis = Dis(pe)
        for key, (pat, mn) in PATTERNS.items():
            for s in pe.sections:
                if not pe.is_exec(s) or not s["rsize"]:
                    continue
                blob = pe.data[s["rptr"]:s["rptr"] + s["rsize"]]
                for m in pat.finditer(blob):
                    a = s["va"] + m.start()
                    inside = any(b <= a < e for b, e in cover)
                    # the region: the pdata range, or the gap that holds `a`
                    if inside:
                        lo, hi = next((b, e) for b, e in cover if b <= a < e)
                    else:
                        lo = max([e for b, e in cover if e <= a] or [s["va"]])
                        hi = min([b for b, e in cover if b > a] or [s["va"] + s["vsize"]])
                    ins = dis.linear(lo, hi)
                    at = [i for i in ins if i.rva == a]
                    if not at:
                        cls = "not_a_start"
                    elif at[0].mn == mn:
                        cls = "decoded"
                    else:
                        cls = "boundary:" + at[0].mn
                    where = "pdata" if inside else "gap"
                    summary[(os.path.basename(path), key, where, cls)] += 1
                    if cls == "decoded" and not inside:
                        detail.append((os.path.basename(path), key, a, lo, hi, os.path.basename(path)))
                    dis.cache_clear()
    print("%-24s %-7s %-6s %-28s %s" % ("image", "bytes", "where", "class", "count"))
    for k in sorted(summary):
        print("%-24s %-7s %-6s %-28s %d" % (k[0], k[1], k[2], k[3], summary[k]))
    print("\nDecoded cpuid/rdtsc starts outside .pdata (image, mnemonic, rva, gap begin, gap end):")
    for row in detail:
        print("  %-22s %-6s %x  gap %x-%x" % (row[0], row[1], row[2], row[3], row[4]))
    # Analyse each decoded gap site like a pdata function: the gap is its own function.
    print("\nAnalysis of the decoded gap sites (leaf/sub-leaf for cpuid; sinks for rdtsc):")
    for path in paths:
        name = os.path.basename(path)
        rows = [r for r in detail if r[0] == name]
        if not rows:
            continue
        pe = load(path)
        dis = Dis(pe)
        by_gap = collections.defaultdict(list)
        for r in rows:
            by_gap[(r[3], r[4])].append(r)
        for (lo, hi), rs in sorted(by_gap.items()):
            ranges = [(lo, hi)]
            # Gaps have no unwind entry, so use a linear decode with fall-through edges.
            lin = dis.linear(lo, hi)
            insns = {i.rva: i for i in lin}
            succ = {}
            for a_, b_ in zip(lin, lin[1:]):
                succ[a_.rva] = [b_.rva]
            if lin:
                succ[lin[-1].rva] = []
            pr = preds(succ)
            ce = ConstEval(insns, pr)
            for r in rs:
                if r[1] == "cpuid" and r[2] in insns:
                    leaf = single(ce.values_before("a", r[2]))
                    sub = single(ce.values_before("c", r[2]))
                    uses, stores = forward_uses(r[2], insns, succ)
                    lf = ("%#x" % leaf) if isinstance(leaf, int) else str(leaf)
                    sf = ("%#x" % sub) if isinstance(sub, int) else str(sub)
                    print("  cpuid %s@%x gap %x: leaf=%s sub=%s uses=%s stores=%d" % (
                        name, r[2], lo, lf, sf, sorted({(str(u[1]), str(u[2]), str(u[3])) for u in uses}, key=str)[:6], len(stores)))
                if r[1] == "rdtsc" and r[2] in insns:
                    sinks = set()

                    def on_use(origin, ins2, what, sinks=sinks):
                        if ins2.mn in ("sub", "subsd", "subss"):
                            sinks.add("sub")
                        elif ins2.mn in ("xor", "imul", "rol", "ror", "mul", "shld", "shrd"):
                            sinks.add("mix")
                        elif ins2.mn == "call":
                            sinks.add("call")
                        elif ins2.mn.startswith("ret"):
                            sinks.add("ret")

                    def on_store(origin, ins2, k, sinks=sinks):
                        sinks.add("store")

                    walk(r[2], succ, insns, {"a": "tsc", "d": "tsc"}, {}, on_use=on_use, on_store=on_store)
                    print("  rdtsc %s@%x gap %x: %s sinks=%s" % (name, r[2], lo, tsc_purpose(sinks),
                                                              sorted(sinks)))
            dis.cache_clear()


if __name__ == "__main__":
    main()
