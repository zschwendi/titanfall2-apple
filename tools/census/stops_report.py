#!/usr/bin/env python3
"""Skipped functions and stops in a lift census, for the 20 modules of the boot path.

    .venv/bin/python tools/census/stops_report.py CENSUS [--before OLD] [--stops] [--check MODULE+RVA ...]

CENSUS and OLD are directories written by tools/census/lift_census.sh (one <module>/
<module>.report.json each).  Reads the lifter's reports only; prints counts, RVAs and
mnemonics.

  default        per module: functions, lifted, of those lifted with stops, stops, skipped by reason;
                 then every function that is still skipped, with its first reason
  --before OLD   what became of each function that OLD has as skipped: lifted with stops,
                 lifted without, or still skipped (and why)
  --stops        the stops by the features they wait for and by instruction
  --check M+RVA  the record of one function (status, stops by instruction); exit 1 if any
                 of the named functions is not lifted

A stop is an instruction the lifter decodes and does not translate, inside a function it lifts
all the same (lifter/stops.py, docs/DESIGN.md section 3).
"""

import argparse
import json
import os
import sys
from collections import Counter

# The 20 modules of the boot path (docs/handoff/round3-plan.md section 3).
BOOT = ("tier0 vstdlib launcher rtech_game filesystem_stdio engine bink2w64 inputsystem vphysics amd_ags_x64 "
        "materialsystem_dx11 datacache studiorender vguimatsurface vgui2 localize mileswin64 binkawin64 client "
        "server").split()


def load(census, module):
    with open(os.path.join(census, module, module + ".report.json")) as f:
        return json.load(f)


def first_reason(rec):
    r = rec["reasons"][0]
    return "%s: %s at %s" % (r["kind"], r["what"], r["rva"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("census")
    ap.add_argument("--before")
    ap.add_argument("--stops", action="store_true")
    ap.add_argument("--check", nargs="*", default=[])
    args = ap.parse_args()

    reports = {m: load(args.census, m) for m in BOOT}
    total = Counter()
    print("%-22s %9s %9s %11s %8s %8s  %s" % ("module", "functions", "lifted", "with stops", "stops", "skipped", "skipped by reason"))
    for m in BOOT:
        s = reports[m]["summary"]
        row = (s["functions"], s["lifted"], s.get("lifted_with_stops", 0), s.get("stops", 0), s["skipped"])
        for k, v in zip(("functions", "lifted", "with_stops", "stops", "skipped"), row):
            total[k] += v
        if row[2] or row[4]:
            print("%-22s %9d %9d %11d %8d %8d  %s" % ((m,) + row + (", ".join(
                "%s %d" % kv for kv in s["skipped_by_reason_kinds"].items()),)))
    print("%-22s %9d %9d %11d %8d %8d  (%d modules; those not listed have neither)" % (
        "all 20", total["functions"], total["lifted"], total["with_stops"], total["stops"], total["skipped"], len(BOOT)))

    print("\nstill skipped:")
    for m in BOOT:
        for r in reports[m]["functions"]:
            if r["status"] == "skipped":
                print("  %s+%s  %s%s" % (m, r["rva"], first_reason(r), "" if not r.get("untranslated") else
                                        "  (and %d instructions that would be stops)" % r["untranslated_insns"]))

    if args.before:
        fate = Counter()
        lines = []
        for m in BOOT:
            now = {r["rva"]: r for r in reports[m]["functions"]}
            for old in load(args.before, m)["functions"]:
                if old["status"] != "skipped":
                    continue
                r = now.get(old["rva"])
                if r is None:
                    k = "no longer a function"
                elif r["status"] == "skipped":
                    k = "still skipped"
                elif r.get("untranslated"):
                    k = "lifted with stops"
                else:
                    k = "lifted, no stop"
                fate[k] += 1
                if k != "lifted with stops":
                    lines.append("  %s+%s  %s  (was: %s)" % (m, old["rva"], k, first_reason(old)))
        print("\nof the %d functions skipped before: %s" % (sum(fate.values()), ", ".join("%d %s" % (v, k) for k, v in fate.most_common())))
        print("\n".join(lines))

    if args.stops:
        by_need, by_insn = Counter(), Counter()
        funcs = Counter()
        for m in BOOT:
            for name, nf, n, need in reports[m]["summary"].get("stops_by_instruction", []):
                by_need[(m, need)] += n
                by_insn[(m, name, need)] += n
            for r in reports[m]["functions"]:
                if r["status"] != "skipped" and r.get("untranslated"):
                    funcs[(m, " + ".join(sorted({x["needs"] or "(none)" for x in r["untranslated"]})))] += 1
        print("\nstops by what they wait for (module, CPUID features, instructions):")
        for (m, need), n in sorted(by_need.items(), key=lambda kv: (kv[0][0], -kv[1])):
            print("  %-12s %-36s %6d" % (m, need or "(no feature bit known)", n))
        print("\nfunctions with stops by the set of features their stops wait for:")
        for (m, need), n in sorted(funcs.items(), key=lambda kv: (kv[0][0], -kv[1])):
            print("  %-12s %4d  %s" % (m, n, need))
        print("\nstops by instruction:")
        for (m, name, need), n in sorted(by_insn.items(), key=lambda kv: (kv[0][0], -kv[1])):
            print("  %-12s %-18s %6d  %s" % (m, name, n, need or "(no feature bit known)"))

    bad = 0
    for item in args.check:
        m, rva = item.split("+")
        rva = "0x%x" % int(rva, 16)
        r = next((x for x in reports[m]["functions"] if x["rva"] == rva), None)
        if r is None:
            print("\n%s: not a function the lifter found" % item)
            bad += 1
            continue
        print("\n%s+%s: %s, %d instructions, %d of them stops" % (m, rva, r["status"], r["insns"], r.get("untranslated_insns", 0)))
        for x in r.get("untranslated", []):
            print("    %-14s x%-4d first at %s   %s" % (x["insn"], x["count"], x["rva"], x["needs"] or "(no feature bit known)"))
        if r["status"] == "skipped":
            print("    SKIPPED: " + first_reason(r))
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
