#!/usr/bin/env python3
"""Tables of the queue-race survey (finding 36) from the JSON files qrace_scan.py wrote.

    .venv/bin/python tools/scout/qrace_report.py OUT_DIR              # per-module counts and every hit
    .venv/bin/python tools/scout/qrace_report.py OUT_DIR --json FILE  # also write the merged raw lists

Reads only the scan's JSON (addresses, offsets, classes); it does not open a game file.
"""
import json
import os
import sys


def main():
    out = sys.argv[1]
    mods = []
    for f in sorted(os.listdir(out)):
        if f.endswith(".json") and not f.startswith("_"):
            mods.append(json.load(open(os.path.join(out, f))))
    tot = {}
    print("%-24s %5s %6s %5s %5s %6s %3s %3s" % ("module", "fns", "enters", "noent", "trunc", "single", "A", "S"))
    for r in mods:
        a = sum(1 for h in r["hits"] if h["class"] == "A")
        row = (r["functions_with_lock_call"], r["enter_sites"], r["functions_without_enter_site"],
               r["truncated_regions"], r["single_read_cas_stores"], a, len(r["hits"]) - a)
        for i, v in enumerate(row):
            tot[i] = tot.get(i, 0) + v
        print("%-24s %5d %6d %5d %5d %6d %3d %3d" % ((r["module"],) + row))
    print("%-24s %5d %6d %5d %5d %6d %3d %3d" % (("total",) + tuple(tot[i] for i in range(7))))
    un = {}
    for r in mods:
        for f, how in r.get("unrecognised_enter", []):
            un[how] = un.get(how, 0) + 1
    print("functions that use an acquiring import with no acquiring call recognised, by use:", un)
    print()
    for r in mods:
        for h in r["hits"]:
            fate = ",".join("%s@%s%s" % (a, b, "=" if c else "") for a, b, c in h["fate"]) or "-"
            print("%s %-20s fn %-9s enter %-9s r1 %-9s r2 %-9s store %-9s sz %d %-12s cmp-with %-10s br %d/%d fate %s"
                  % (h["class"], r["module"][:-4], h["fn"], h["enter"], h["read1"], h["read2"], h["store"],
                     h["size"], h["addr"], h["comperand"], h["branches_between"], h["branches_to_jcc"], fate[:60]))
    if "--json" in sys.argv:
        path = sys.argv[sys.argv.index("--json") + 1]
        keep = ("module", "lock_imports", "wrappers", "functions_with_lock_call", "enter_sites",
                "functions_without_enter_site", "truncated_regions", "single_read_cas_stores",
                "single_read_cas", "unrecognised_enter", "hits", "errors")
        with open(path, "w") as fh:
            json.dump({"scan": [{k: r[k] for k in keep} for r in mods]}, fh, indent=1)
            fh.write("\n")


if __name__ == "__main__":
    main()
