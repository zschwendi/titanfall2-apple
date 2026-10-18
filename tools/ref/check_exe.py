#!/usr/bin/env python3
"""Check the built reference program without running it (`make ref-check`).

usage: check_exe.py build/ref/refcalls.exe

Reads the program's own headers: it must be a 64-bit x86 console program with an entry point
and unwind tables, and it must import from kernel32.dll only, each name being one that
tools/ref/kernel32.def lists (a misspelt name would otherwise show first on the other machine,
as a program that does not start).  Exit 0 when all of that holds.
"""
import pathlib
import struct
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    exe = pathlib.Path(sys.argv[1])
    data = exe.read_bytes()
    listed = set()
    for line in (pathlib.Path(__file__).parent / "kernel32.def").read_text().splitlines():
        word = line.split(";")[0].strip()
        if word and word != "EXPORTS" and not word.startswith("LIBRARY"):
            listed.add(word)
    problems = []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        print(f"{exe}: not a PE file")
        return 1
    machine, nsections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]
    stack_reserve, stack_commit = struct.unpack_from("<QQ", data, opt + 72)
    if machine != 0x8664 or magic != 0x20B:
        problems.append(f"machine {machine:#x}, optional-header magic {magic:#x}: not x64 PE32+")
    if subsystem != 3:
        problems.append(f"subsystem {subsystem}: not a console program")
    if entry == 0:
        problems.append("no entry point")
    sections = []
    for i in range(nsections):
        off = opt + opt_size + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((va, max(vsize, rsize), raw))

    def at(rva: int) -> int:
        for va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(f"rva {rva:#x} is in no section")

    def text(rva: int) -> str:
        start = at(rva)
        return data[start:data.index(b"\0", start)].decode("ascii")

    dirs = [struct.unpack_from("<II", data, opt + 112 + 8 * i) for i in range(16)]
    if dirs[3][1] == 0:
        problems.append("no exception directory: the functions have no unwind tables")
    imports = {}
    off = at(dirs[1][0]) if dirs[1][0] else None
    while off is not None:
        lookup, _, _, name_rva, first = struct.unpack_from("<IIIII", data, off)
        if not name_rva:
            break
        names = imports.setdefault(text(name_rva).lower(), [])
        thunk = at(lookup or first)
        while True:
            value = struct.unpack_from("<Q", data, thunk)[0]
            if not value:
                break
            names.append(f"#{value & 0xFFFF}" if value >> 63 else text((value & 0x7FFFFFFF) + 2))
            thunk += 8
        off += 20
    for dll, names in imports.items():
        if dll != "kernel32.dll":
            problems.append(f"imports from {dll}: {', '.join(names)}")
        for name in names:
            if name not in listed:
                problems.append(f"imports {name} from {dll}, which tools/ref/kernel32.def does not list")
    count = sum(len(n) for n in imports.values())
    print(f"{exe}: x64 console program, {len(data)} bytes, entry +{entry:#x}, stack {stack_reserve:#x} reserved "
          f"{stack_commit:#x} committed, {dirs[3][1] // 12} functions with unwind data, {count} imports from "
          f"{', '.join(sorted(imports)) or 'nothing'}")
    for p in problems:
        print(f"check_exe: PROBLEM: {p}")
    if not problems:
        print("check_exe: ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
