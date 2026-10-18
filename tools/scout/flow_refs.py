#!/usr/bin/env python3
"""Who refers to a data address or calls a function, in one module (finding 37).

    .venv/bin/python tools/scout/flow_refs.py MODULE data 0xRVA [0xRVA ...]   # readers and writers
    .venv/bin/python tools/scout/flow_refs.py MODULE call 0xRVA [0xRVA ...]   # direct callers
    .venv/bin/python tools/scout/flow_refs.py MODULE ptr  0xRVA [0xRVA ...]   # lea sites and data pointers
    .venv/bin/python tools/scout/flow_refs.py MODULE imp  NAME_REGEX          # call sites of an import

Each line: the site, the instruction mnemonic (for data: `W` when the address is the first, written
or tested, operand), the enclosing function and the first strings that function loads.  Uses
xref_lib's cached tables (set SCOUT_CACHE to choose where they live).  Prints addresses, names and
strings the code refers to.  OriginSDK.dll is refused.
"""

import re
import sys

from flow_lib import get, fmt_imp


def where(M, site):
    f = M.func_of(site)
    if f is None:
        return "(no function)"
    return "%s %s" % (M.label(f), " | ".join(repr(s) for s in M.first_strings(f, 2)))


def main():
    M = get(sys.argv[1])
    kind = sys.argv[2]
    if kind == "imp":
        rx = re.compile(sys.argv[3])
        slots = {s: v for s, v in M.slots.items() if rx.search(str(v[1]))}
        thunk_of = {}
        for t, v in M.thunks.items():
            thunk_of.setdefault(v, []).append(t)
        for s, v in sorted(slots.items(), key=lambda kv: str(kv[1])):
            sites = [(a, mn) for a, mn, fm in M.data_refs(s) if M.func_of(a) is not None]
            for t in thunk_of.get(v, []):
                sites += [(a, mn) for a, mn, f in M.callers(t)]
            print("%s: %d site(s)" % (fmt_imp(v), len(sites)))
            for a, mn in sorted(sites):
                print("    %7x %-5s in %s" % (a, mn, where(M, a)))
        return
    for a in sys.argv[3:]:
        rva = int(a, 16)
        if kind == "data":
            refs = M.data_refs(rva)
            print("%s+0x%x: %d reference(s)" % (M.short, rva, len(refs)))
            for site, mn, fm in refs:
                print("    %7x %-6s %s in %s" % (site, mn, "W" if fm and mn != "lea" else " ", where(M, site)))
        elif kind == "call":
            cs = M.callers(rva)
            print("%s+0x%x: %d direct caller(s)" % (M.short, rva, len(cs)))
            for site, mn, f in cs:
                print("    %7x %-5s in %s" % (site, mn, where(M, site)))
        elif kind == "ptr":
            leas = [(s, mn) for s, mn, fm in M.data_refs(rva) if mn == "lea"]
            absr = M.m.abs_refs([rva]).get(rva, [])
            print("%s+0x%x: %d lea, %d data pointer(s)" % (M.short, rva, len(leas), len(absr)))
            for site, mn in leas:
                print("    lea  %7x in %s" % (site, where(M, site)))
            for p in absr:
                # is the pointer a slot of an RTTI vtable?  walk back to the nearest one
                note = ""
                for back in range(0, 600):
                    c = M.vtable_class(p - 8 * back)
                    if c:
                        note = "%s slot %d" % (c, back)
                        break
                print("    data %s+0x%x  %s" % (M.short, p, note))


if __name__ == "__main__":
    main()
