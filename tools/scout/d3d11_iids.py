#!/usr/bin/env python3
"""Find Direct3D 11 / DXGI interface IDs inside the game's modules (issue #31, question 2).

Looks for the 16-byte memory image of every IID that d3d11_vtables.py knows, in every section
of every module under game/, and prints module, section, RVA, interface name.  A module that
holds no IID cannot be calling QueryInterface or GetParent with one.  Only addresses and names
are printed; the IID values themselves are the public ones from the SDK headers.

    .venv/bin/python tools/scout/d3d11_iids.py [module ...]
    .venv/bin/python tools/scout/d3d11_iids.py --refs MODULE       # only the IDs that code refers to

With --refs, each ID image is followed by the instructions that refer to it (any RIP-relative
operand) and by 8-byte data pointers to it.  An image nothing refers to is link leftover, not
use: materialsystem_dx11.dll holds 78 such images and 6 that code refers to.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3d11_vtables as V  # noqa: E402
import scout_ctx  # noqa: E402
from xref_lib import Module  # noqa: E402

# IIDs the headers above do not cover but that matter when naming what a QueryInterface asks for.
EXTRA = {
    "00000000-0000-0000-c000-000000000046": "IUnknown",
}


def table():
    ifs = V.load()
    t = {}
    for n, it in ifs.items():
        if it.iid:
            t[V.guid_bytes(it.iid)] = n
    for g, n in EXTRA.items():
        t[V.guid_bytes(g)] = n
    return t


def scan(mod, t):
    """[(rva, section, name)] for each IID image found in mod."""
    out = []
    pe = mod.pe
    for s in pe.sections:
        if s["rsize"] == 0:
            continue
        raw = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
        # IIDs are 4-aligned in practice; scan all 16 byte windows at 4-byte steps
        for i in range(0, len(raw) - 15, 4):
            n = t.get(raw[i:i + 16])
            if n:
                out.append((s["va"] + i, s["name"], n))
    return out


def refs_report(name):
    import xref_lib
    mod, tables = xref_lib.load_cached(scout_ctx.module_path(name))
    t = table()
    hits = scan(mod, t)
    addr = {a: (sec, n) for a, sec, n in hits}
    users = {}
    for a, size, mn, tgt, fm in tables["rip"]:
        if tgt in addr:
            users.setdefault(tgt, []).append((a, mn))
    ptrs = xref_lib.data_pointers_to(mod, list(addr))
    print("%s: %d ID images, %d referenced by code, %d by data pointers" % (
        mod.name, len(addr), len(users), len(ptrs)))
    for a in sorted(users):
        print("   0x%x %-26s referenced at %s (functions %s)" % (
            a, addr[a][1], ", ".join("0x%x" % x for x, mn in users[a]),
            ", ".join(sorted({"0x%x" % (mod.unit_of(x) or 0) for x, mn in users[a]}))))


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--refs":
        for n in sys.argv[2:]:
            refs_report(n)
        return
    names = sys.argv[1:] or scout_ctx.all_module_names()
    t = table()
    for n in names:
        mod = Module(scout_ctx.module_path(n))
        hits = scan(mod, t)
        if hits:
            print("%s: %d IID images" % (mod.name, len(hits)))
            for rva, sec, name in hits:
                print("   0x%x  %-8s %s" % (rva, sec, name))


if __name__ == "__main__":
    main()
