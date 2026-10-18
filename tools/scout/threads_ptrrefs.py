#!/usr/bin/env python3
"""Pointer-style references to code addresses (issue #29): rip-relative lea in code and
8-byte absolute pointers in data, for given RVAs in one module.

    .venv/bin/python tools/scout/threads_ptrrefs.py MODULE RVA [RVA ...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402


def main():
    m = Module(sys.argv[1])
    tgts = set(int(x, 16) for x in sys.argv[2:])
    exp = {r: n for n, o, r in m.exports()}
    for site, tgt in m.lea_refs():
        if tgt in tgts:
            f = m.func_of(site)
            print("  lea %x -> %x in f%s %s" % (site, tgt, "%x" % f if f is not None else "?", exp.get(f, "")))
    for t, sites in m.abs_refs(tgts).items():
        for s in sites:
            print("  data ptr at %s+0x%x -> %x" % (m.sec_name(s), s, t))
    for site, kind, tgt in m.rel_refs(tgts):
        f = m.func_of(site)
        print("  %s %x -> %x in f%s %s" % (kind, site, tgt, "%x" % f if f is not None else "?", exp.get(f, "")))


if __name__ == "__main__":
    main()
