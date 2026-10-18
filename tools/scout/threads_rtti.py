#!/usr/bin/env python3
"""MSVC x64 RTTI walk: class names, vtable addresses and base classes (issue #29).

For each module: find every type descriptor (.?AV / .?AU names), every complete object
locator that references it, the vtable that follows a pointer to that locator, and the
base-class list from the class hierarchy descriptor. Writes CACHE/<module>.rtti.json and
prints classes whose name or any base matches REGEX.

    .venv/bin/python tools/scout/threads_rtti.py CACHE_DIR REGEX [module ...]
"""

import json
import multiprocessing as mp
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, SOURCE_MODULES, THIRD_PARTY  # noqa: E402


def demangle(n):
    """Cheap readable name from '.?AVCFoo@Bar@@' -> 'Bar::CFoo'. Templates left decorated."""
    n = n[4:]
    if n.endswith("@@"):
        n = n[:-2]
    if "?$" in n:
        return n
    return "::".join(reversed(n.split("@")))


def walk(args):
    name, cache = args
    m = Module(name)
    base = m.base
    # type descriptors
    tds = {}   # td_rva -> mangled name
    for rva, s in m.strings().items():
        if s.startswith(".?AV") or s.startswith(".?AU"):
            tds[rva - 16] = s
    # complete object locators: find dwords equal to a td rva at (pos-12)
    cols = {}   # col_rva -> td_rva
    vtabs = {}  # col_rva -> [vtable rva]
    qwords = {}
    for sec in m.data_sections():
        buf = m.data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
        n4 = len(buf) // 4
        d4 = struct.unpack_from("<%dI" % n4, buf, 0)
        va0 = sec["va"]
        for i, v in enumerate(d4):
            if v in tds and i >= 3 and i + 2 < n4:
                col = va0 + 4 * (i - 3)
                if d4[i - 3] in (0, 1) and d4[i + 2] == col:   # signature and pSelf
                    cols[col] = v
    want = {base + c: c for c in cols}
    for sec in m.data_sections():
        buf = m.data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
        n8 = len(buf) // 8
        d8 = struct.unpack_from("<%dQ" % n8, buf, 0)
        va0 = sec["va"]
        for i, v in enumerate(d8):
            if v in want:
                vtabs.setdefault(want[v], []).append(va0 + 8 * (i + 1))
    classes = []
    for col, td in cols.items():
        sig, off, cdoff, ptd, pchd, pself = struct.unpack("<IIIIII", m.bytes_at(col, 24))
        chd = m.bytes_at(pchd, 16)
        _sig, _attr, nbases, parr = struct.unpack("<IIII", chd)
        bases = []
        for k in range(min(nbases, 64)):
            (brva,) = struct.unpack("<I", m.bytes_at(parr + 4 * k, 4))
            (btd,) = struct.unpack("<I", m.bytes_at(brva, 4))
            bn = tds.get(btd)
            if bn:
                bases.append(demangle(bn))
        vt = sorted(vtabs.get(col, []))
        slots = []
        for v in vt:
            cnt = 0
            while cnt < 600:
                q = m.u64(v + 8 * cnt)
                if q is None:
                    break
                r = q - base
                sec2 = m.pe.section_of(r) if q >= base else None
                if sec2 is None or not m.pe.is_exec(sec2):
                    break
                cnt += 1
            slots.append(cnt)
        classes.append({"name": demangle(tds[td]), "mangled": tds[td], "col": col, "offset": off,
                        "bases": bases, "vtables": vt, "slots": slots})
    classes.sort(key=lambda c: c["name"])
    with open(os.path.join(cache, name + ".rtti.json"), "w") as f:
        json.dump({"module": name, "n_types": len(tds), "classes": classes}, f)
    return name, len(tds), len(classes)


def main():
    cache = sys.argv[1]
    rx = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
    names = sys.argv[3:] or (SOURCE_MODULES + THIRD_PARTY)
    os.makedirs(cache, exist_ok=True)
    with mp.Pool(4) as pool:
        for name, nt, nc in pool.imap_unordered(walk, [(n, cache) for n in names]):
            print("rtti %-26s types %5d with locators %5d" % (name, nt, nc), flush=True)
    if rx:
        for name in names:
            d = json.load(open(os.path.join(cache, name + ".rtti.json")))
            hits = [c for c in d["classes"] if rx.search(c["name"]) or any(rx.search(b) for b in c["bases"][1:])]
            if not hits:
                continue
            print("== %s" % name)
            for c in hits:
                print("  %-60s vtable %s slots %s bases %s" % (
                    c["name"][:60], ",".join("0x%x" % v for v in c["vtables"][:3]) or "-",
                    c["slots"][:3], ",".join(c["bases"][1:5])))


if __name__ == "__main__":
    main()
