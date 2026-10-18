#!/usr/bin/env python3
"""Caller chains upward from a function (issue #29): direct calls/jumps, lea function-pointer
loads and data vtable-ish pointers, annotated with exports and strings.

    .venv/bin/python tools/scout/threads_chain.py MODULE RVA [DEPTH]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402


def main():
    m = Module(sys.argv[1])
    start = int(sys.argv[2], 16)
    depth = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    exp = {r: n for n, o, r in m.exports()}
    cidx, lidx = m.call_index(), m.lea_index()
    strs = m.strings()
    # strings loaded by each function (first few), built lazily from lea refs
    fstr = {}
    for site, tgt in m.lea_refs():
        if tgt in strs:
            f = m.func_of(site)
            if f is not None:
                fstr.setdefault(f, []).append(strs[tgt][:40])
    seen = set()

    def label(f):
        extra = []
        if f in exp:
            extra.append("EXPORT " + exp[f][:60])
        if f in fstr:
            extra.append("strs " + " | ".join(fstr[f][:3]))
        return "f%x %s" % (f, " ".join(extra))

    def walk(f, d, indent):
        if d > depth or f in seen:
            return
        seen.add(f)
        callers = {}
        for site, kind in cidx.get(f, []):
            cf = m.func_of(site)
            callers.setdefault(cf, kind)
        for site in lidx.get(f, []):
            cf = m.func_of(site)
            callers.setdefault(cf, "lea")
        dptr = m.abs_refs([f]).get(f, [])
        print("%s%s  <- %d callers%s" % (indent, label(f), len(callers), (", data ptrs at " + ",".join("%x" % x for x in dptr[:4])) if dptr else ""))
        for cf, kind in list(callers.items())[:8]:
            if cf is None:
                continue
            print("%s  [%s]" % (indent, kind))
            walk(cf, d + 1, indent + "    ")

    walk(start, 0, "")


if __name__ == "__main__":
    main()
