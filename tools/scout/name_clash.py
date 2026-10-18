#!/usr/bin/env python3
"""Find instructions whose Capstone id name collides with a string instruction's.

The lifter dispatches on `insn_name()`. Capstone gives some SSE compares (printed as
cmpneqsd, cmpltsd, ...) the id name of a string instruction (cmpsq, cmpsd, ...). This counts
how many such instructions the game contains, to size that class of mis-translation.

    .venv/bin/python tools/scout/name_clash.py game
"""

import collections
import os
import struct
import sys
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE, find_pe_files  # noqa: E402

import capstone  # noqa: E402

STRING_OPCODES = {0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF}
STRING_NAMES = {k + s for k in ("movs", "stos", "lods", "scas", "cmps") for s in "bwdq"}


def scan(path):
    pe = PE(path)
    if pe.machine != 0x8664:
        return None
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return None
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    clash, ranges, genuine = collections.Counter(), set(), 0
    for i in range(size // 12):
        begin, end, _ = struct.unpack_from("<III", pe.data, off + 12 * i)
        boff = pe.rva_to_off(begin)
        if boff is None or end <= begin:
            continue
        for ins in md.disasm(pe.data[boff:boff + (end - begin)], begin):
            name = ins.insn_name()
            if name in STRING_NAMES:
                if ins.opcode[0] in STRING_OPCODES:
                    genuine += 1
                else:
                    clash[(name, ins.mnemonic)] += 1
                    ranges.add(begin)
    return os.path.basename(path), genuine, clash, len(ranges)


def main():
    paths = [p for p in find_pe_files(sys.argv[1]) if os.path.basename(p).lower() != "d3dcompiler_46.dll"]
    total, genuine, ranges = collections.Counter(), 0, 0
    with Pool() as pool:
        for r in pool.map(scan, paths):
            if not r:
                continue
            name, g, clash, n = r
            genuine += g
            ranges += n
            total.update(clash)
            if clash:
                print("%-26s %5d clashing instructions in %4d unwind ranges" % (name, sum(clash.values()), n))
    print("\ngenuine string instructions: %d" % genuine)
    print("instructions named like a string op that are not one: %d, in %d unwind ranges"
          % (sum(total.values()), ranges))
    for (name, mnemonic), c in total.most_common(24):
        print("   id name %-6s printed as %-12s %d" % (name, mnemonic, c))


if __name__ == "__main__":
    main()
