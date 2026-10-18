#!/usr/bin/env python3
"""Cross-module uses of tier0/vstdlib thread, job and pool exports (issue #29).

For every module that imports from tier0.dll or vstdlib.dll, find the import slots whose
name matches REGEX, then the call/load sites and enclosing functions. Prints a table of
(import name -> module -> sites) and, with -v, every site with the strings its function loads.

    .venv/bin/python tools/scout/threads_xmod.py REGEX [-v]
"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, all_module_names  # noqa: E402


def short(n):
    if n.startswith("?"):
        return n.split("@@")[0][1:].replace("@", "::") if "@@" in n else n
    return n


def main():
    rx = re.compile(sys.argv[1], re.I)
    verbose = "-v" in sys.argv
    table = defaultdict(lambda: defaultdict(list))
    funcs_info = {}
    for name in all_module_names():
        m = Module(name)
        slots = {s: (d, n) for s, (d, n) in m.import_slots().items()
                 if d in ("tier0.dll", "vstdlib.dll") and rx.search(n)}
        if not slots:
            continue
        refs = m.iat_refs(slots.keys())
        strs = m.strings()
        fstr = defaultdict(list)
        if verbose:
            for site, tgt in m.lea_refs():
                if tgt in strs:
                    f = m.func_of(site)
                    if f is not None:
                        fstr[f].append(strs[tgt][:36])
        exp = {r: n for n, o, r in m.exports()}
        for site, kind, slot in refs:
            d, n = slots[slot]
            f = m.func_of(site)
            table[(d, short(n))][name].append((site, f, kind))
            if verbose:
                funcs_info[(name, f)] = (exp.get(f, ""), fstr.get(f, [])[:3])
    for (d, n), mods in sorted(table.items()):
        print("%s!%s" % (d, n))
        for mod, sites in mods.items():
            fs = sorted({f for _, f, _ in sites if f is not None})
            print("    %-24s %3d sites in %2d functions: %s" % (mod, len(sites), len(fs), " ".join("%x" % f for f in fs[:10])))
            if verbose:
                for f in fs[:12]:
                    e, ss = funcs_info.get((mod, f), ("", []))
                    print("          f%x %s %s" % (f, e[:50], ss))


if __name__ == "__main__":
    main()
