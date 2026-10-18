#!/usr/bin/env python3
"""Count, for each boot-path game DLL, the places in its code that reference each import
slot of the Windows libraries named on the command line.

    .venv/bin/python tools/scout/import_sites.py advapi32 winmm        # tables by library
    .venv/bin/python tools/scout/import_sites.py kernel32 --only GetFileSize,SetErrorMode
    .venv/bin/python tools/scout/import_sites.py hid --rvas            # with the rva of each place

A "place" is an instruction with a rip-relative memory operand that lands exactly on the
import's slot (`call [slot]`, `jmp [slot]`, `mov reg, [slot]`), or a direct `call`/`jmp` to
an import stub (a bare `jmp [slot]` outside every unwind range); the stub itself is not
counted, as in import_surface.py, so an import reached only through a pointer to its stub
shows 0.  Found by a byte scan
of the executable sections, not by disassembly: a ModRM byte of the form mod=00 rm=101
followed by a displacement that hits a slot, and an E8/E9 whose target is a stub.  A chance
match needs four displacement bytes to hit one of a few hundred addresses, so false places
are not expected; a place whose instruction has an immediate after the displacement
(`cmp qword [slot], 0`) is missed.  A `mov reg, [slot]` counts once however often the
register is then called.

The modules are the boot path of tools/scout/boot_import_gap.py (finding 30's load order).
It opens those DLL files and no other: never OriginSDK.dll, never Titanfall2.exe.  Prints
module names, import names, counts and (with --rvas) addresses.  Read only, single process,
standard library.
"""

import argparse
import bisect
import os
import re
import struct
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))
sys.path.insert(0, HERE)

FORBIDDEN = {"originsdk.dll", "titanfall2.exe"}
MODRM_RIP = re.compile(rb"[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]")
CALL_JMP = re.compile(rb"[\xe8\xe9]")


def find_module(game, name):
    for sub in ("bin/x64_retail", ".", "r2/bin/x64_retail", "bin"):
        p = os.path.join(game, sub, name)
        if os.path.exists(p):
            return p
    for d, _, files in os.walk(game):
        if name in files:
            return os.path.join(d, name)
    return None


def scan(path, libs):
    """(lib, name) -> [rva of each place], for one module; an import with no place has []."""
    from pe_inventory import PE
    pe = PE(path)
    slots = {}
    rva, _ = pe.dirs[1]
    off = pe.rva_to_off(rva)
    while off is not None:
        oft, _, _, name, ft = struct.unpack_from("<IIIII", pe.data, off)
        if not (oft or name or ft):
            break
        dll = pe.cstr(name).lower()
        for i, n in enumerate(pe._thunks(oft or ft)):
            if dll in libs:
                slots[ft + 8 * i] = (dll, n)
        off += 20
    out = defaultdict(list)
    for imp in slots.values():
        out[imp] = []
    if not slots:
        return out
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva)
    ranges = sorted(struct.unpack_from("<II", pe.data, off + 12 * i) for i in range(size // 12))
    begins = [b for b, _ in ranges]

    def in_function(r):
        i = bisect.bisect_right(begins, r) - 1
        return i >= 0 and ranges[i][0] <= r < ranges[i][1]

    stubs = {}
    hits = []
    text = [s for s in pe.sections if pe.is_exec(s)]
    for s in text:
        d = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
        for m in MODRM_RIP.finditer(d):
            i = m.start()
            if i + 5 > len(d) or i == 0:
                continue
            (disp,) = struct.unpack_from("<i", d, i + 1)
            target = s["va"] + i + 5 + disp
            if target not in slots:
                continue
            at = s["va"] + i - 1
            is_stub = d[i - 1] == 0xFF and d[i] == 0x25 and not in_function(at)
            if is_stub:
                stubs[at] = slots[target]
            hits.append((at, slots[target], is_stub))
    # A bare jmp [slot] outside every unwind range is an import stub (as in
    # import_surface.py): it is not a place, a direct call or jump to it is.  The same
    # instruction inside a function is a tail call and counts.
    stub_refs = defaultdict(list)
    for s in text:
        d = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
        for m in CALL_JMP.finditer(d):
            i = m.start()
            if i + 5 > len(d):
                continue
            (rel,) = struct.unpack_from("<i", d, i + 1)
            t = s["va"] + i + 5 + rel
            if t in stubs:
                stub_refs[t].append(s["va"] + i)
    for rva_, imp, is_stub in hits:
        if not is_stub:
            out[imp].append(rva_)
    for t, refs in stub_refs.items():
        out[stubs[t]].extend(refs)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("libs", nargs="+", help="library names, with or without .dll")
    ap.add_argument("--game", default=os.path.join(ROOT, "game"))
    ap.add_argument("--only", help="comma-separated import names to keep")
    ap.add_argument("--rvas", action="store_true", help="print the rva of each place")
    args = ap.parse_args()
    import boot_import_gap as gap

    libs = {l.lower() if l.lower().endswith(".dll") else l.lower() + ".dll" for l in args.libs}
    only = set(args.only.split(",")) if args.only else None
    table = defaultdict(dict)                                  # (lib, name) -> {module: [rva]}
    for _, mods in gap.STAGES:
        for mod in mods:
            if mod.lower() in FORBIDDEN or mod.lower() in gap.NOT_LIFTED or mod in gap.STUBBED:
                continue
            path = find_module(args.game, mod)
            if path is None:
                print("not found: %s" % mod, file=sys.stderr)
                continue
            for imp, rvas in scan(path, libs).items():
                table[imp][mod[:-4]] = sorted(rvas)
    for lib in sorted(libs):
        print("== %s" % lib)
        for (l, name), mods in sorted(table.items()):
            if l != lib or (only and name not in only):
                continue
            total = sum(len(v) for v in mods.values())
            parts = ", ".join("%s %d" % (m, len(v)) for m, v in mods.items())
            print("  %-34s %3d  %s" % (name, total, parts))
            if args.rvas:
                for m, v in mods.items():
                    if v:
                        print("      %s: %s" % (m, " ".join("0x%x" % r for r in v)))


if __name__ == "__main__":
    main()
