#!/usr/bin/env python3
"""Count instructions by printed mnemonic in some DLLs, and optionally require a minimum
count for a list of them.

By default the executable sections are swept from start to end (right for small DLLs we
compiled ourselves, where leaf functions have no unwind entry and the code section holds
nothing but code and padding).  --unwind-only sweeps the unwind-table ranges instead, which
is the safer choice for the game's DLLs.

Used as the acceptance check for test-corpus units: a corpus file written to exercise an
instruction family only counts if the compiler really emitted those instructions.

    .venv/bin/python tools/census/require_mnemonics.py tests/corpus/build/*.dll
    .venv/bin/python tools/census/require_mnemonics.py tests/corpus/build/*.dll \
        --require addss,subss,mulss --min 3

The mnemonic is the one the decoder prints, without prefixes (so the SSE compares appear as
cmpltss, cmpneqps, ... and 'lock xadd' as xadd).  Exit status 1 if any required mnemonic has
fewer than --min instances in total, 0 otherwise.
"""

import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE  # noqa: E402

import capstone  # noqa: E402


def ranges(pe, unwind_only):
    """(file offset, length, rva) of each stretch of code to sweep."""
    if not unwind_only:
        for s in pe.sections:
            if pe.is_exec(s):
                yield s["rptr"], min(s["vsize"], s["rsize"]), s["va"]
        return
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return
    for i in range(size // 12):
        begin, end, _ = struct.unpack_from("<III", pe.data, off + 12 * i)
        boff = pe.rva_to_off(begin)
        if boff is not None and end > begin:
            yield boff, end - begin, begin


def count(path, unwind_only=False):
    pe = PE(path)
    c = collections.Counter()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for off, length, rva in ranges(pe, unwind_only):
        pos = 0
        while pos < length:                  # resume one byte on after anything undecodable
            last = pos
            for ins in md.disasm(pe.data[off + pos:off + length], rva + pos):
                c[ins.mnemonic.split()[-1]] += 1
                last = ins.address - rva + ins.size
            pos = last + 1 if last == pos else last
    return c


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dlls", nargs="+")
    ap.add_argument("--require", default="", help="comma-separated mnemonics that must be present")
    ap.add_argument("--min", type=int, default=1, help="minimum total instances of each required mnemonic")
    ap.add_argument("--unwind-only", action="store_true", help="sweep unwind-table ranges, not whole sections")
    args = ap.parse_args()
    total = collections.Counter()
    for path in args.dlls:
        total.update(count(path, args.unwind_only))
    need = [m for m in args.require.split(",") if m]
    if not need:
        for m, n in total.most_common():
            print("%-14s %d" % (m, n))
        return 0
    short = [(m, total[m]) for m in need if total[m] < args.min]
    for m in need:
        print("%-14s %6d %s" % (m, total[m], "" if total[m] >= args.min else "<-- fewer than %d" % args.min))
    if short:
        print("\nMISSING: %d of %d required mnemonics have fewer than %d instances: %s"
              % (len(short), len(need), args.min, ", ".join(m for m, _ in short)))
        return 1
    print("\nOK: all %d required mnemonics have at least %d instances" % (len(need), args.min))
    return 0


if __name__ == "__main__":
    sys.exit(main())
