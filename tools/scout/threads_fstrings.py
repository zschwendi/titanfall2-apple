#!/usr/bin/env python3
"""Strings loaded (rip-relative lea) by given functions, fragments included (issue #29).

    .venv/bin/python tools/scout/threads_fstrings.py MODULE RVA [RVA ...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402


def main():
    m = Module(sys.argv[1])
    funcs = set(int(x, 16) for x in sys.argv[2:])
    strs = m.strings()
    out = {f: [] for f in funcs}
    for site, tgt in m.lea_refs():
        f = m.func_of(site)
        if f in funcs and tgt in strs:
            out[f].append((site, strs[tgt][:110]))
    for f in sorted(out):
        print("f%x" % f)
        for site, s in out[f]:
            print("    %x  %r" % (site, s))


if __name__ == "__main__":
    main()
