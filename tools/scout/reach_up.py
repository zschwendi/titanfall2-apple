#!/usr/bin/env python3
"""Walk up from a function to what can call it, with names the binary itself carries
(finding 38, questions 2 and 4).

    .venv/bin/python tools/scout/reach_up.py MODULE RVA [RVA ...] [--depth N] [--strings] [--paths K]
    .venv/bin/python tools/scout/reach_up.py --ijmp CENSUS_DIR [--depth N] [--strings]

For each start function: its static references (direct call or jump, address taken, pointer
in data), then a breadth-first walk up the caller edges (direct calls and tail jumps, and
"address taken in" edges).  The walk stops at a function that sits in a virtual table found
through the image's own RTTI (shown as class#slot) or is an export: from there on the
caller is an indirect call that static reading cannot follow.  Prints, per depth, how many
functions were reached; the labelled roots; with --strings the strings loaded by the
functions on the way (first few of each), which is how a subsystem is recognised; with
--paths K, K shortest chains from a root down to the start.

--ijmp walks every function of the 20 boot-path modules that the lifter skipped for an
indirect jump.

Uses scout_ctx (xref_lib's cached decode, RTTI, exports, strings).  Prints addresses, names
that are in the game's own files (exports, RTTI class names, string constants) and counts.
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach_lib as R  # noqa: E402
import reach_skips as K  # noqa: E402
import scout_ctx  # noqa: E402

_ctx = {}


def ctx(module):
    if module.lower() in R.NEVER:
        raise SystemExit("%s is never read" % module)
    if module not in _ctx:
        c = scout_ctx.Ctx(module)
        c.rev = c.build_rev()
        _ctx[module] = c
    return _ctx[module]


def up(module, start, depth=8, strings=False, paths=0, out=print):
    c = ctx(module)
    m = c.mod
    unit = m.unit_of(start)
    data = sorted(R.T.Module(module + ".dll").abs_refs([start]).get(start, []))
    direct = [(s, mn, cf) for s, mn, cf in c.callers(start)]
    taken = c.g.taken.get(start, [])
    out("%s+0x%x  %s" % (module, start, c.label(start)))
    out("   direct: %s" % (", ".join("%s@0x%x in +0x%x" % (mn, s, cf if cf is not None else 0) for s, mn, cf in direct[:10])
                          + (" ... %d more" % (len(direct) - 10) if len(direct) > 10 else "") or "none"))
    out("   address taken in code: %s;  pointers in data: %s" % (
        ", ".join("+0x%x (in +0x%x)" % (s, cf or 0) for s, cf in taken[:6]) or "none",
        ", ".join("+0x%x %s" % (d, m.section_name(d)) for d in data[:6]) or "none"))
    seen, roots = c.climb(c.rev, unit if unit is not None else start, maxdepth=depth)
    by_depth = collections.Counter(d for _child, d, _k in seen.values())
    out("   reached going up: %s" % ", ".join("depth %d: %d" % (d, n) for d, n in sorted(by_depth.items())))
    labelled = [(seen[r][1], r, c.label(r)) for r in roots if c.label(r)]
    bare = [(seen[r][1], r) for r in roots if not c.label(r)]
    labelled.sort()
    out("   roots: %d labelled, %d with no caller found" % (len(labelled), len(bare)))
    for d, r, lab in labelled[:24]:
        out("      depth %d  +0x%x  %s" % (d, r, lab[:150]))
    if len(labelled) > 24:
        out("      ... %d more labelled roots" % (len(labelled) - 24))
    for d, r in sorted(bare)[:10]:
        dd = sorted(R.T.Module(module + ".dll").abs_refs([r]).get(r, [])) if len(bare) <= 40 else []
        out("      depth %d  +0x%x  no caller%s" % (d, r, ("; pointer in data at " + ", ".join("+0x%x" % x for x in dd[:3])) if dd else ""))
    if strings:
        for f, (_child, d, _k) in sorted(seen.items(), key=lambda kv: kv[1][1]):
            ss = c.fstr.get(f)
            if ss and d <= depth:
                out("      strings at depth %d in +0x%x: %s" % (d, f, " | ".join(s[:48].replace("\n", "\\n") for s in ss[:5])))
    if paths:
        for d, r, lab in labelled[:paths]:
            out("      path: " + " <- ".join("+0x%x" % x for x in reversed(c.chain(seen, r))))


def main():
    args = sys.argv[1:]
    depth = 8
    if "--depth" in args:
        depth = int(args[args.index("--depth") + 1])
    paths = 0
    if "--paths" in args:
        paths = int(args[args.index("--paths") + 1])
    strings = "--strings" in args
    if "--ijmp" in args:
        cdir = R.census_dir(args[args.index("--ijmp") + 1])
        for mod in R.BOOT:
            rep = R.load_report(mod, cdir)
            for rva, _kind, what, rec in R.skipped(rep):
                if K.classify(rec).startswith("ijmp"):
                    print("--- skipped for: %s at %s (origin %s, %d instructions)" % (what, rec["reasons"][0]["rva"], rec["origin"], rec["insns"]))
                    up(mod, rva, depth, strings, paths)
        return
    pos = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or args[i - 1] not in ("--depth", "--paths"))]
    if len(pos) < 2:
        raise SystemExit(__doc__)
    for a in pos[1:]:
        up(pos[0], int(a, 16), depth, strings, paths)


if __name__ == "__main__":
    main()
