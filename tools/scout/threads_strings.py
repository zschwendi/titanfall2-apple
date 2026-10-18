#!/usr/bin/env python3
"""Strings that look thread-related, with the functions that load their address (issue #29).

    .venv/bin/python tools/scout/threads_strings.py REGEX [module ...]

Prints `module  rva  string  <- function(site)...`. Loads of a string come from rip-relative
`lea` instructions (and, when none are found, 8-byte absolute pointers in data). Run from a
file, never from stdin.
"""

import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, all_module_names, SOURCE_MODULES  # noqa: E402


RTTI = "--rtti" in sys.argv


def main():
    sys.argv = [a for a in sys.argv if a != "--rtti"]
    rx = re.compile(sys.argv[1], re.I)
    names = sys.argv[2:] or SOURCE_MODULES
    for name in names:
        m = Module(name)
        skip = {a for (_d, a) in m.import_slots().values()} | {n for n, _o, _r in m.exports()}
        hits = {rva: s for rva, s in m.strings().items()
                if rx.search(s) and s not in skip and not s.startswith("?") and (RTTI or not s.startswith(".?A"))}
        if not hits:
            continue
        leas = {}
        for site, tgt in m.lea_refs():
            if tgt in hits:
                leas.setdefault(tgt, []).append(site)
        absr = m.abs_refs([r for r in hits if r not in leas])
        print("== %s (%d hits)" % (name, len(hits)))
        for rva in sorted(hits):
            s = hits[rva]
            refs = leas.get(rva, [])
            funcs = []
            for site in refs:
                f = m.func_of(site)
                funcs.append("f%x@%x" % (f, site) if f is not None else "?@%x" % site)
            extra = ""
            if rva in absr:
                extra = " DATA-PTR@" + ",".join("%x" % x for x in absr[rva][:4])
            print("  %7x  %-60s <- %s%s" % (rva, repr(s[:100]), " ".join(funcs[:6]) + (" ..." if len(funcs) > 6 else ""), extra))


if __name__ == "__main__":
    main()
