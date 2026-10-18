#!/usr/bin/env python3
"""Question 5: are the rare instructions (xgetbv, rdrand, in, out, syscall, hlt, int, int1)
real code or data that linear sweep decoded as instructions?

Two independent checks per image:

  A. Raw byte search. The encodings of the three-byte instructions (xgetbv 0F 01 D0,
     xsetbv 0F 01 D1, rdrand 0F C7 F0-F7, rdseed 0F C7 F8-FF) and of syscall (0F 05) are
     counted in every executable section and compared with what the .pdata sweep decodes.

  B. Recursive descent from each function's entry (the .pdata begin of every fragment of
     the function), in two variants:
       * all edges, including MSVC switch-table targets;
       * without the edges that leave indirect jumps, so the result shows which sites
         are reached by straight-line control flow alone.
     A site reached only through a switch edge depends on the table decode; a site not
     reached in either variant is data mis-decoded by the linear sweep (or dead code).

    .venv/bin/python tools/scout/cpu_q5_rare.py <scratchpad>/sites.json game
"""

import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import SWITCH_BOUNDED, Dis, descend, function_table, game_images, load, preds  # noqa: E402

RARE = {"xgetbv", "xsetbv", "rdrand", "rdseed", "syscall", "in", "out", "hlt", "int", "int1"}
PATTERNS = {
    "xgetbv": re.compile(rb"\x0f\x01\xd0"),
    "xsetbv": re.compile(rb"\x0f\x01\xd1"),
    "rdrand": re.compile(rb"\x0f\xc7[\xf0-\xf7]"),
    "rdseed": re.compile(rb"\x0f\xc7[\xf8-\xff]"),
    "syscall": re.compile(rb"\x0f\x05"),
}


def raw_counts(pe):
    """Raw byte-pattern hits in executable sections, keyed by mnemonic."""
    out = collections.Counter()
    where = collections.defaultdict(list)
    for s in pe.sections:
        if not pe.is_exec(s) or not s["rsize"]:
            continue
        blob = pe.data[s["rptr"]:s["rptr"] + s["rsize"]]
        for mn, pat in PATTERNS.items():
            for m in pat.finditer(blob):
                out[mn] += 1
                where[mn].append(s["va"] + m.start())
    return out, where


def reach_strict(insns, succ, starts):
    """Reachability from the primary entries only, following switch edges only where the
    table bound was recovered (an unbounded table can over-run into data)."""
    seen = set()
    work = [s for s in starts if s in insns]
    while work:
        a = work.pop()
        if a in seen or a not in insns:
            continue
        seen.add(a)
        ins = insns[a]
        if ins.is_indirect_jmp and not SWITCH_BOUNDED.get(a, False):
            continue
        work.extend(succ.get(a, []))
    return seen


def analyse_image(path, sites):
    pe = load(path)
    entries, funcs = function_table(pe)
    dis = Dis(pe)
    raw, where = raw_counts(pe)
    out = {"image": os.path.basename(path), "raw": dict(raw), "sites": [], "raw_checks": []}
    # Raw hits of the three-byte encodings: are they inside .pdata code, and if so are they
    # instruction boundaries reached from a primary entry?
    entries_by_range = [(b, e, p) for b, e, p in entries]
    dis0 = Dis(pe)
    for mn in ("xgetbv", "rdrand", "rdseed", "syscall"):
        for a in where.get(mn, []):
            enc = {"xgetbv": 3, "rdrand": 3, "rdseed": 3, "syscall": 2}[mn]
            hit = [x for x in entries_by_range if x[0] <= a < x[1]]
            rec = {"mn": mn, "rva": a, "in_pdata": bool(hit), "primary": None, "strict": None}
            if hit:
                b, e, parent = hit[0]
                ranges = funcs.get(parent, [(b, e)])
                insns, succ, _ = descend(dis0, ranges, [parent])
                strict = reach_strict(insns, succ, [parent])
                rec["primary"] = parent
                rec["strict"] = a in strict
                rec["decoded_at_hit"] = a in insns and insns[a].mn == mn
            out["raw_checks"].append(rec)
        dis0.cache_clear()
    by_func = collections.defaultdict(list)
    for s in sites:
        if s["mn"] in RARE:
            by_func[s["func"]].append(s)
    for func, ss in by_func.items():
        ranges = funcs.get(func, [(func, func + 1)])
        # Primary entry: the function's own begin (fragments are not roots of their own).
        insns, succ, unresolved = descend(dis, ranges, [func])
        insns_all, succ_all, _ = descend(dis, ranges, [b for b, _ in ranges])
        strict = reach_strict(insns, succ, [func])
        plain = reach_strict(insns_all, succ_all, [b for b, _ in ranges])
        pr = preds(succ_all)
        for s in ss:
            rva = s["rva"]
            rec = {"mn": s["mn"], "rva": rva, "func": func, "text": s["text"],
                   "reached_all": rva in insns_all, "reached_strict": rva in strict,
                   "reached_plain": rva in plain,
                   "unresolved_in_func": len(unresolved),
                   "pred_count": len(pr.get(rva, [])),
                   "pred_texts": [insns_all[p].mn + " " + insns_all[p].text for p in pr.get(rva, [])][:3]}
            out["sites"].append(rec)
        dis.cache_clear()
    return out


def scan(job):
    path, sites = job
    return analyse_image(path, sites)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    data = json.load(open(sys.argv[1]))
    paths = {os.path.basename(p): p for p in game_images(sys.argv[2])}
    per = collections.defaultdict(list)
    for s in data["sites"]:
        if s["mn"] in RARE:
            per[s["img"]].append(s)
    # Every image with a raw pattern hit, not only the ones with a decoded site.
    results = []
    for img in sorted(paths):
        results.append(analyse_image(paths[img], per.get(img, [])))
    with open(os.path.join(os.path.dirname(sys.argv[1]), "q5_rare.json"), "w") as f:
        json.dump(results, f, indent=1, default=str)

    print("A. raw byte hits in executable sections vs decoded in .pdata sweep")
    raw_tot = collections.Counter()
    for r in results:
        raw_tot.update(r["raw"])
    print("   raw totals (all images):", dict(raw_tot) or "none")
    for r in results:
        if r["raw"]:
            print("   %-24s raw=%s" % (r["image"], r["raw"]))
    print("\nA2. raw three-byte hits: inside .pdata? reached from a primary entry? decoded as that mnemonic?")
    for r in results:
        for c in r["raw_checks"]:
            print("   %-22s %-7s rva=%x in_pdata=%s strict=%s decoded_as_that=%s" % (
                r["image"], c["mn"], c["rva"], c["in_pdata"], c["strict"], c.get("decoded_at_hit")))
    print("\nB. decoded rare-instruction sites, reachability")
    rows = collections.Counter()
    for r in results:
        for s in r["sites"]:
            rows[(s["mn"], s["text"] if s["mn"] == "int" else "", s["reached_strict"], s["reached_all"])] += 1
    print("   %-8s %-6s %-16s %-14s %s" % ("mnemonic", "imm", "strict(primary)", "any-fragment", "count"))
    for k in sorted(rows):
        print("   %-8s %-6s %-16s %-14s %d" % (k[0], k[1], k[2], k[3], rows[k]))
    print("\nC. sites reached from primary entries (strict=True), by image")
    for r in results:
        plain = [s for s in r["sites"] if s["reached_strict"]]
        if plain:
            print("   %-22s %s" % (r["image"], ", ".join("%s@%x(f%x,pred=%d)" % (
                s["mn"], s["rva"], s["func"], s["pred_count"]) for s in plain[:12])))
    print("\nD. sites not reached from any .pdata range start, by image")
    for r in results:
        nr = [s for s in r["sites"] if not s["reached_all"]]
        if nr:
            print("   %-22s %s" % (r["image"], ", ".join("%s@%x" % (s["mn"], s["rva"]) for s in nr[:14])))


if __name__ == "__main__":
    main()
