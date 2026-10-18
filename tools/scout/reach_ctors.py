#!/usr/bin/env python3
"""Is a function one of a module's static initializers, and where in the order?  (finding 38)

    .venv/bin/python tools/scout/reach_ctors.py MODULE [RVA ...] [--walk]

A static initializer is an entry of a table of code pointers that the C start-up walks from
its first slot to its last when the DLL is loaded, before the DLL's own entry code runs.
The script finds such tables from the code, not from names:

  1. two rip-relative `lea` in one function whose targets A < Z are 8-byte aligned slots of
     a data section that hold 0 (the start-up's own markers), with only slots that hold 0
     or a pointer into code between them, make a candidate table [A, Z);
  2. the function holding the pair is shown with its distance (direct calls and jumps) from
     the DLL's entry point, so that "walked at load" is read from the code.

For each RVA given it prints the table that holds its address, the slot's index and the
table's length.  With --walk it also lists, for each table reachable from the entry point,
the entries that are (or directly call, to depth 2) a function the lifter skipped: needs
TF2_CENSUS_DIR.  Prints addresses and counts only.
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach_lib as R  # noqa: E402


def code_ptr(m, v):
    if not v or not (m.base <= v < m.base + m.pe.size_of_image):
        return False
    s = m.pe.section_of(v - m.base)
    return s is not None and m.pe.is_exec(s)


def tables(m):
    """[(A, Z, function, site of the lea of A)] candidate initializer tables: A and Z are
    slots that hold 0 (the start-up's own markers), both loaded with `lea` in one function,
    adjacent among that function's such targets, and every slot between them holds 0 or a
    pointer into code, at least one of them a pointer."""
    data = [(s["va"], s["va"] + s["vsize"]) for s in m.data_sections()]

    def in_data(t):
        return any(lo <= t < hi for lo, hi in data)

    byf = collections.defaultdict(list)
    for a, _size, mn, t, _fm in m.x()["rip"]:
        if mn == "lea" and t % 8 == 0 and in_data(t) and m.u64(t) == 0:
            byf[m.unit_of(a)].append((a, t))
    out = []
    for f, lst in byf.items():
        ts = sorted(set(t for _a, t in lst))
        for lo, hi in zip(ts, ts[1:]):
            if hi - lo > 8 * 20000:
                continue
            n = 0
            for slot in range(lo, hi, 8):
                v = m.u64(slot)
                if v is None or not (v == 0 or code_ptr(m, v)):
                    n = 0
                    break
                n += 1 if v else 0
            if n:
                out.append((lo, hi, f, min(a for a, t in lst if t == lo)))
    return sorted(out)


def distance_from_entry(m, targets, limit=6):
    """{function: depth} for functions reached from the entry point by direct calls/jumps."""
    fwd = collections.defaultdict(set)
    for addr, _mn, tgt in m.x()["calls"]:
        fwd[m.unit_of(addr)].add(tgt)
    ep = m.pe.entry_rva
    seen = {ep: 0}
    level = [ep]
    for d in range(1, limit + 1):
        nxt = []
        for f in level:
            for t in fwd.get(f, ()):
                u = m.unit_of(t)
                for x in (t, u):
                    if x is not None and x not in seen:
                        seen[x] = d
                        nxt.append(x)
        level = nxt
    return {t: seen[t] for t in targets if t in seen}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        raise SystemExit(__doc__)
    m = R.Mod(args[0])
    want = [int(a, 16) for a in args[1:]]
    tabs = tables(m)
    dist = distance_from_entry(m, [t[2] for t in tabs])
    print("%s: entry point +0x%x; %d candidate tables" % (m.short, m.pe.entry_rva, len(tabs)))
    for lo, hi, f, site in tabs:
        n = sum(1 for s in range(lo, hi, 8) if m.u64(s))
        print("  table [+0x%x, +0x%x): %d slots, %d non-zero; bounds loaded in %s (lea at +0x%x), %s"
              % (lo, hi, (hi - lo) // 8, n, m.label(f), site,
                 ("%d direct calls from the entry point" % dist[f]) if f in dist else "not reached from the entry point by direct calls"))
    for w in want:
        sites = m.abs_refs([w]).get(w, [])
        if not sites:
            print("+0x%x: its address is in no data section" % w)
        for s in sites:
            hit = [(lo, hi, f) for lo, hi, f, _site in tabs if lo <= s < hi]
            if not hit:
                print("+0x%x: pointer at +0x%x (%s), in no candidate table" % (w, s, m.sec_name(s)))
            for lo, hi, f in hit:
                idx = sum(1 for x in range(lo, s, 8) if m.u64(x))
                n = sum(1 for x in range(lo, hi, 8) if m.u64(x))
                print("+0x%x: pointer at +0x%x is entry %d of %d (counting from 0, non-zero slots) of table [+0x%x, +0x%x)"
                      % (w, s, idx, n, lo, hi))
    if "--walk" in sys.argv:
        rep = R.load_report(m.short)
        skip = {r[0]: r for r in R.skipped(rep)}
        fwd = collections.defaultdict(set)
        for addr, mn, tgt in m.x()["calls"]:
            fwd[m.unit_of(addr)].add(tgt)
        for lo, hi, f, _site in tabs:
            if f not in dist:
                continue
            idx = 0
            for s in range(lo, hi, 8):
                v = m.u64(s)
                if not v:
                    continue
                fn = v - m.base
                level, seen = {fn}, {fn}
                for d in range(3):
                    for x in sorted(level):
                        if x in skip:
                            print("  table +0x%x entry %d (+0x%x): reaches skipped +0x%x at depth %d (%s: %s)"
                                  % (lo, idx, fn, x, d, skip[x][1], skip[x][2]))
                    level = {t for x in level for t in fwd.get(x, ())} - seen
                    seen |= level
                idx += 1


if __name__ == "__main__":
    main()
