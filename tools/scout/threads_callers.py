#!/usr/bin/env python3
"""Direct callers (e8/e9 rel32) of given function addresses in one module (issue #29).

    .venv/bin/python tools/scout/threads_callers.py MODULE RVA [RVA ...]

Prints each caller site with its enclosing function and, when that function is an export or
loads a string, the name/string. Raw byte scan; false positives are possible in principle.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402


def main():
    m = Module(sys.argv[1])
    tgts = [int(x, 16) for x in sys.argv[2:]]
    exp = {r: n for n, o, r in m.exports()}
    refs = m.rel_refs(tgts)
    print("%s: %d direct refs" % (m.name, len(refs)))
    for site, kind, tgt in refs:
        f = m.func_of(site)
        print("  %7x -> %x  %-4s in f%s %s" % (site, tgt, kind, "%x" % f if f is not None else "?", exp.get(f, "")[:70]))


if __name__ == "__main__":
    main()
