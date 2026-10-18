#!/usr/bin/env python3
"""What the lifter skipped, per boot-path module, from the lifter's own reports (finding 38,
question 5).

    tools/census/lift_census.sh OUT          # the reports; the generated C is deleted as it goes
    .venv/bin/python tools/scout/reach_skips.py OUT [--list] [--all-modules]

Reads OUT/<module>/<module>.report.json for the 20 boot-path modules (with --all-modules:
every module the census lifted).  Prints one line per module (functions, lifted, skipped,
and the skipped ones by class) and the sums.  A function's class is decided from *all* its
reasons, not the first:

  isa       every reason is an instruction of an extension our CPUID does not report
            (VEX-encoded AVX/AVX2/FMA/BMI, AES-NI, PCLMULQDQ, SHA)
  mmx       every reason is an instruction form on the MMX registers
  ijmp      an indirect jump the lifter could not resolve
  other     anything else: printed in full, because the tables of the plan do not count it

--list prints each skipped function: module+rva, class, origin, number of reasons, the
distinct mnemonics refused.  Also prints, per module, the functions with status
"lifted-without-handlers" and the undecoded bytes inside lifted functions, which are not
skips but are the other two things a report can hide.
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach_lib as R  # noqa: E402

AES = {"aesenc", "aesenclast", "aesdec", "aesdeclast", "aesimc", "aeskeygenassist"}
PCL = {"pclmulqdq"}
SHA = {"sha1rnds4", "sha1nexte", "sha1msg1", "sha1msg2", "sha256rnds2", "sha256msg1", "sha256msg2"}
BMI = {"andn", "bextr", "blsi", "blsmsk", "blsr", "tzcnt", "bzhi", "mulx", "pdep", "pext", "rorx",
       "sarx", "shlx", "shrx", "adcx", "adox"}
# What the lifter says when the form is one on mm0..mm7.
MMX_MARK = "is not a form the lifter translates"


def mnem(what):
    return what.split()[0] if what else "?"


def reason_class(r):
    kind = r.get("kind", "?")
    what = r.get("what", "")
    m = mnem(what)
    if kind == "indirect-jump":
        return "ijmp"
    if kind == "unsupported":
        if m.startswith("v") or m in AES or m in PCL or m in SHA or m in BMI:
            return "isa"
        if MMX_MARK in what or m == "emms":
            return "mmx"
    return "other"


def classify(rec):
    cls = collections.Counter(reason_class(r) for r in rec.get("reasons") or [{}])
    if "ijmp" in cls:
        return "ijmp" if set(cls) <= {"ijmp"} else "ijmp+" + "+".join(sorted(set(cls) - {"ijmp"}))
    if len(cls) == 1:
        return next(iter(cls))
    return "+".join(sorted(cls))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cdir = R.census_dir(args[0] if args else None)
    show = "--list" in sys.argv
    mods = R.BOOT
    if "--all-modules" in sys.argv:
        mods = sorted(d for d in os.listdir(cdir)
                      if os.path.isfile(os.path.join(cdir, d, d + ".report.json")) and d.lower() not in R.NEVER)
    tot = collections.Counter()
    by_class = collections.Counter()
    print("%-22s %8s %8s %7s  %s" % ("module", "functions", "lifted", "skipped", "skipped by class"))
    listing = []
    for mname in mods:
        try:
            rep = R.load_report(mname, cdir)
        except FileNotFoundError:
            print("%-22s no report in %s" % (mname, cdir))
            continue
        fns = rep["functions"]
        st = collections.Counter(f.get("status") for f in fns)
        cls = collections.Counter()
        for rva, _k, _w, rec in R.skipped(rep):
            c = classify(rec)
            cls[c] += 1
            ms = collections.Counter(mnem(r.get("what", "")) for r in rec.get("reasons", []))
            listing.append((mname, rva, c, rec.get("origin"), len(rec.get("reasons", [])),
                            ", ".join("%s %d" % kv for kv in ms.most_common(6)),
                            rec["reasons"][0].get("what", "") if c.startswith(("ijmp", "other")) or "other" in c else ""))
        undec = sum(f.get("undecoded_bytes", 0) for f in fns if f.get("status") != "skipped")
        tot["functions"] += len(fns)
        tot["lifted"] += len(fns) - st.get("skipped", 0)
        tot["skipped"] += st.get("skipped", 0)
        tot["nohandlers"] += st.get("lifted-without-handlers", 0)
        tot["undecoded"] += undec
        by_class.update(cls)
        extra = []
        if st.get("lifted-without-handlers"):
            extra.append("%d lifted without handlers" % st["lifted-without-handlers"])
        if undec:
            extra.append("%d undecoded bytes in lifted functions" % undec)
        others = set(st) - {"lifted", "skipped", "lifted-without-handlers"}
        if others:
            extra.append("OTHER STATUS: %s" % ", ".join("%s %d" % (o, st[o]) for o in others))
        print("%-22s %8d %8d %7d  %s%s" % (mname, len(fns), len(fns) - st.get("skipped", 0), st.get("skipped", 0),
                                         ", ".join("%s %d" % kv for kv in sorted(cls.items())) or "-",
                                         ("   [" + "; ".join(extra) + "]") if extra else ""))
    print("%-22s %8d %8d %7d  %s" % ("sum of %d" % len(mods), tot["functions"], tot["lifted"], tot["skipped"],
                                    ", ".join("%s %d" % kv for kv in sorted(by_class.items()))))
    print("lifted without handlers: %d; undecoded bytes inside lifted functions: %d" % (tot["nohandlers"], tot["undecoded"]))
    if show:
        print()
        for mname, rva, c, origin, n, ms, first in listing:
            print("%s+0x%x  %-6s origin=%-6s reasons=%-4d %s%s" % (mname, rva, c, origin, n, ms, ("  | " + first) if first else ""))


if __name__ == "__main__":
    main()
