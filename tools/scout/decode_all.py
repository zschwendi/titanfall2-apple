#!/usr/bin/env python3
"""Decode the game's x64 modules once and cache the cross-reference tables (see xref_lib.py).

    .venv/bin/python tools/scout/decode_all.py game [name ...]

Uses at most 4 worker processes. Run it from a file, never from stdin.
"""

import os
import sys
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xref_lib  # noqa: E402


def work(path):
    t = time.time()
    mod, tables = xref_lib.load_cached(path)
    return mod.name, tables["ninsn"], len(tables["rip"]), len(tables["calls"]), len(tables["ind"]), time.time() - t


def all_modules(root):
    out = [os.path.join(root, "server.dll")]
    d = os.path.join(root, "bin", "x64_retail")
    out += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.lower().endswith(".dll")]
    return out


def main():
    root = sys.argv[1]
    only = {n.lower() for n in sys.argv[2:]}
    paths = all_modules(root)
    if only:
        paths = [p for p in paths if os.path.basename(p).lower() in only]
    # biggest first so the long jobs start early
    paths.sort(key=lambda p: -os.path.getsize(p))
    with Pool(4) as pool:
        for r in pool.imap_unordered(work, paths):
            print("%-28s insns=%9d rip=%8d calls=%8d ind=%7d  %.0fs" % r, flush=True)


if __name__ == "__main__":
    main()
