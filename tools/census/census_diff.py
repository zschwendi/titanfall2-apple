#!/usr/bin/env python3
"""Compare two censuses (output directories of tools/census/lift_census.sh): what a change to
the lifter did to every module.

    .venv/bin/python tools/census/census_diff.py BEFORE AFTER

Reads only each module's <module>.report.json.  Prints the totals of both, one line per module
whose counts changed, and by module+rva: the functions skipped before and lifted now, the
functions lifted before and skipped now, the functions only one census found, and the lifted
functions whose jump tables differ (address, entries, targets, layout or bound), and the
functions lifted in both in which a jump is read as a tail call differently: another count of
'jmp reg' taken as a tail call, or a jump through a table of function pointers taken as one
(discover._tail_through_table) that the function did not have.  That rule may only turn a skipped
function into a lifted one: a function that was lifted before had every jump explained (as a
switch or as a tail call), so one that shows such a tail call now has a jump read differently.
Exit status 1 if a function that was lifted is skipped now, the two censuses did not find the
same functions, or a function lifted in both has such a tail call it did not have; else 0.
Nothing is written to disk.
"""

import glob
import json
import os
import sys


def load(census):
    out = {}
    for path in sorted(glob.glob(os.path.join(census, "*", "*.report.json"))):
        rep = json.load(open(path))
        out[rep["module"]] = {int(r["rva"], 16): r for r in rep["functions"]}
    return out


def lifted(r):
    return r["status"] != "skipped"


def counts(funcs):
    rs = funcs.values()
    return (sum(1 for r in rs if lifted(r)), sum(1 for r in rs if not lifted(r)),
            sum(1 for r in rs if not lifted(r) and any(x["kind"] == "indirect-jump" for x in r["reasons"])),
            sum(len(r.get("jump_tables", ())) for r in rs if lifted(r)))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    before, after = load(sys.argv[1]), load(sys.argv[2])
    bad = 0
    if set(before) != set(after):
        bad = 1
        print("modules only before: %s; only after: %s" % (sorted(set(before) - set(after)), sorted(set(after) - set(before))))
    modules = sorted(set(before) & set(after))
    tb = [sum(counts(before[m])[i] for m in modules) for i in range(4)]
    ta = [sum(counts(after[m])[i] for m in modules) for i in range(4)]
    print("%d modules; lifted / skipped / skipped for an indirect jump / jump tables in lifted functions" % len(modules))
    print("  before  %d / %d / %d / %d" % tuple(tb))
    print("  after   %d / %d / %d / %d" % tuple(ta))
    gained, lost, tables, tails, reread = [], [], [], [], []
    for m in modules:
        b, a = before[m], after[m]
        if counts(b) != counts(a):
            print("  %-22s %d / %d / %d / %d  ->  %d / %d / %d / %d" % ((m,) + counts(b) + counts(a)))
        if set(b) != set(a):
            bad = 1
            print("  %s: found only before: %s; only after: %s" % (
                m, ["0x%x" % x for x in sorted(set(b) - set(a))], ["0x%x" % x for x in sorted(set(a) - set(b))]))
        for rva in sorted(set(b) & set(a)):
            if lifted(b[rva]) and not lifted(a[rva]):
                lost.append((m, rva, a[rva]))
            elif lifted(a[rva]) and not lifted(b[rva]):
                gained.append((m, rva, a[rva]))
            elif lifted(a[rva]):
                if b[rva].get("jump_tables") != a[rva].get("jump_tables"):
                    tables.append((m, rva))
                if b[rva].get("tail_jumps_through_register") != a[rva].get("tail_jumps_through_register"):
                    tails.append((m, rva))
                had = {t["jmp"] for t in b[rva].get("tail_calls_through_table", ())}
                if any(t["jmp"] not in had for t in a[rva].get("tail_calls_through_table", ())):
                    reread.append((m, rva))
    print("skipped before, lifted now: %d" % len(gained))
    for m, rva, r in gained:
        print("  %s+0x%x  %d instructions%s" % (m, rva, r["insns"], "".join(
            "; table at %s, %d entries, %d targets, %s, bound %s" % (t["table"], t["entries"], t["targets"], t["layout"], t["bound"])
            for t in r.get("jump_tables", ())) + "".join(
            "; tail call through a table at %s, %s%s" % (t["jmp"], t["form"], "".join(
                ", %s %s" % (k, t[k]) for k in ("table", "slots", "functions") if k in t))
            for t in r.get("tail_calls_through_table", ()))))
    print("lifted before, skipped now: %d" % len(lost))
    for m, rva, r in lost:
        print("  %s+0x%x  %s" % (m, rva, "; ".join("%s at %s" % (x["kind"], x["rva"]) for x in r["reasons"])))
    print("lifted in both with other jump tables: %d%s" % (len(tables), "".join("\n  %s+0x%x" % t for t in tables)))
    print("lifted in both with another count of 'jmp reg' taken as a tail call: %d%s"
          % (len(tails), "".join("\n  %s+0x%x" % t for t in tails)))
    print("lifted in both, with a jump now taken as a tail call through a table that was read otherwise before: %d%s"
          % (len(reread), "".join("\n  %s+0x%x" % t for t in reread)))
    return 1 if bad or lost or reread else 0


if __name__ == "__main__":
    sys.exit(main())
