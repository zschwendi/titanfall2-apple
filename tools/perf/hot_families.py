#!/usr/bin/env python3
"""Which x86 instruction families the hottest translated functions are made of.

usage: hot_families.py SAMPLE.txt [SAMPLE.txt ...] [--top N]

Reads the "Sort by top of stack" section of macOS `sample` output. A translated function is a
line `<module>_<hex rva>  (in gate3)  <samples>`. For the N functions with the most samples,
each function's weight is split evenly over its instructions (a static estimate: every
instruction of a function is taken to run equally often). Families are the lifter/insns/*.py
files, found by their @F.insn("mnemonic") registrations; unregistered mnemonics are (core).
Read-only.
"""

import argparse
import collections
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/scout")

SORT_HDR = "Sort by top of stack"
LINE_RE = re.compile(r"^\s*(.+?)\s+\(in ([^)]+)\)\s+(\d+)\s*$")
FUNC_RE = re.compile(r"^([A-Za-z0-9_]+)_([0-9a-fA-F]+)$")
INSN_RE = re.compile(r'@F\.insn\(\s*"([^"]+)"')


def family_map():
    fam = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "lifter", "insns", "*.py"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        with open(path, encoding="utf-8") as fh:
            for mn in INSN_RE.findall(fh.read()):
                fam.setdefault(mn, stem)
    return fam


def read_top(path):
    """Return (translated functions as (name, samples) sorted desc, total translated samples)."""
    funcs = []
    in_sec = False
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if SORT_HDR in line:
                in_sec = True
                continue
            if not in_sec:
                continue
            m = LINE_RE.match(line)
            if not m or m.group(2).strip() != "gate3":
                continue
            name = m.group(1).strip()
            if FUNC_RE.match(name):
                funcs.append((name, int(m.group(3))))
    funcs.sort(key=lambda t: -t[1])
    total = sum(s for _, s in funcs)
    return funcs, total


def decode(name):
    """Return (module, rva, [mnemonics]) or None when the function cannot be decoded."""
    import flow_lib  # noqa: E402

    m = FUNC_RE.match(name)
    mod, rva = m.group(1), int(m.group(2), 16)
    try:
        M = flow_lib.get(mod)
        insns = list(M.insns(rva))
    except (Exception, SystemExit):
        return None
    if not insns:
        return None
    return [i.mnemonic for i in insns]


def analyse(path, top, fam):
    funcs, total = read_top(path)
    chosen = funcs[:top]
    covered = sum(s for _, s in chosen)
    fam_w = collections.defaultdict(float)
    fam_n = collections.Counter()
    fam_mn = collections.defaultdict(collections.Counter)
    mn_w = collections.defaultdict(float)
    mn_n = collections.Counter()
    fn_rows = []
    not_decoded = 0
    not_decoded_n = 0
    for name, samples in chosen:
        mns = decode(name)
        if mns is None:
            not_decoded += samples
            not_decoded_n += 1
            continue
        per = samples / len(mns)
        counts = collections.Counter(mns)
        fn_rows.append((name, samples, len(mns), counts.most_common(5)))
        for mn in mns:
            f = fam.get(mn, "(core)")
            fam_w[f] += per
            fam_n[f] += 1
            fam_mn[f][mn] += per
            mn_w[mn] += per
            mn_n[mn] += 1
    fn_rows.sort(key=lambda r: -r[1])
    return {
        "path": path, "top": top, "total": total, "covered": covered,
        "not_decoded": not_decoded, "not_decoded_n": not_decoded_n,
        "fam_w": fam_w, "fam_n": fam_n, "fam_mn": fam_mn,
        "mn_w": mn_w, "mn_n": mn_n, "fn_rows": fn_rows, "fam": fam,
    }


def pct(x, base):
    return 100.0 * x / base if base else 0.0


def report(r):
    cov = r["covered"]
    dec = cov - r["not_decoded"]
    out = []
    out.append("== %s" % os.path.basename(r["path"]))
    out.append("Top %d translated functions: %d samples, %.1f%% of the %d translated top-of-stack samples"
               % (r["top"], cov, pct(cov, r["total"]), r["total"]))
    if r["not_decoded_n"]:
        out.append("Not decoded: %d functions, %d samples (not in the weights)"
                   % (r["not_decoded_n"], r["not_decoded"]))
    out.append("Weights: samples / instructions per function, every instruction equal (static estimate).")
    out.append("")
    out.append("By family (weight as %% of the %d covered samples of decoded functions):" % dec)
    out.append("%-28s %8s %8s  %s" % ("family", "weight%", "insns", "three heaviest mnemonics"))
    for f, w in sorted(r["fam_w"].items(), key=lambda t: -t[1]):
        heavy = sorted(r["fam_mn"][f].items(), key=lambda t: -t[1])[:3]
        hs = ", ".join("%s %.2f" % (m, pct(v, dec)) for m, v in heavy)
        out.append("%-28s %8.2f %8d  %s" % (f, pct(w, dec), r["fam_n"][f], hs))
    out.append("")
    out.append("By mnemonic, 40 heaviest:")
    out.append("%-18s %-24s %8s %8s" % ("mnemonic", "family", "weight%", "insns"))
    for mn, w in sorted(r["mn_w"].items(), key=lambda t: -t[1])[:40]:
        out.append("%-18s %-24s %8.2f %8d" % (mn, r["fam"].get(mn, "(core)"), pct(w, dec), r["mn_n"][mn]))
    out.append("")
    out.append("15 hottest functions:")
    out.append("%-28s %8s %6s  %s" % ("function", "samples", "insns", "five most frequent mnemonics"))
    for name, s, n, top5 in r["fn_rows"][:15]:
        t5 = " ".join("%s:%d" % (m, c) for m, c in top5)
        out.append("%-28s %8d %6d  %s" % (name, s, n, t5))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("samples", nargs="+")
    ap.add_argument("--top", type=int, default=60)
    a = ap.parse_args(argv)
    fam = family_map()
    for p in a.samples:
        print(report(analyse(p, a.top, fam)))
        print()


if __name__ == "__main__":
    main()
