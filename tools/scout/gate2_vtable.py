#!/usr/bin/env python3
"""Where a function of tier0.dll or vstdlib.dll sits in a table of code pointers (a vtable), and the
other entries of that table.

    .venv/bin/python tools/scout/gate2_vtable.py vstdlib 0x18e50          # tables that hold the function
    .venv/bin/python tools/scout/gate2_vtable.py vstdlib --at 0x5c0a8 24  # 24 slots starting at an rva

A table is taken to be the run of consecutive 8-byte values around the hit that are addresses inside the
module's code section.  Output: rvas, slot numbers and the lifter status of each target.  Nothing is
stored.
"""

import struct
import sys

import gate2_lib as L


def code_range(M):
    for s in M.pe.sections:
        name = s["name"] if isinstance(s, dict) else s.name
        if name == ".text":
            va = s["va"] if isinstance(s, dict) else s.va
            size = s["vsize"] if isinstance(s, dict) else s.vsize
            return va, va + size
    raise SystemExit("no .text section")


def sections(M):
    out = []
    for s in M.pe.sections:
        g = (lambda k: s[k]) if isinstance(s, dict) else (lambda k: getattr(s, k))
        out.append((g("name"), g("va"), g("vsize")))
    return out


def qword_at(M, rva):
    off = M.pe.rva_to_off(rva)
    if off is None or off + 8 > len(M.pe.data):
        return None
    return struct.unpack_from("<Q", M.pe.data, off)[0]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    M = L.load(sys.argv[1])
    base = M.pe.image_base
    lo, hi = code_range(M)

    def target(rva):
        v = qword_at(M, rva)
        if v is None:
            return None
        t = v - base
        return t if lo <= t < hi else None

    def show(start, n):
        for i in range(n):
            t = target(start + 8 * i)
            if t is None:
                print(f"  slot {i:3d}  +0x{start + 8 * i:x}  (not a code address)")
                continue
            print(f"  slot {i:3d}  +0x{start + 8 * i:x}  -> +0x{t:x}  [{M.status(t)}]")

    if sys.argv[2] == "--at":
        show(int(sys.argv[3], 16), int(sys.argv[4]) if len(sys.argv) > 4 else 16)
        return
    fn = int(sys.argv[2], 16)
    hits = 0
    for name, va, size in sections(M):
        if name not in (".rdata", ".data"):
            continue
        for rva in range(va, va + size - 7, 8):
            if target(rva) != fn:
                continue
            start = rva
            while target(start - 8) is not None:
                start -= 8
            end = rva
            while target(end + 8) is not None:
                end += 8
            hits += 1
            print(f"{sys.argv[1]}+0x{fn:x} is slot {(rva - start) // 8} of the table at +0x{start:x} "
                  f"({(end - start) // 8 + 1} code pointers, {name}):")
            show(start, (end - start) // 8 + 1)
    if not hits:
        print(f"{sys.argv[1]}+0x{fn:x}: no table in .rdata or .data holds it")


if __name__ == "__main__":
    main()
