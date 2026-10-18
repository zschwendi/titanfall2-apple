#!/usr/bin/env python3
"""Two scouting questions answered from the binaries:

1. Which thread-block (TEB) fields does the game read through `gs:`?
2. What are the exception/unwind handler routines in the unwind tables: C++ exception
   handling, structured exception handling, or stack-cookie checks?

    .venv/bin/python tools/scout/teb_and_handlers.py game
"""

import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE, find_pe_files  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

TEB = {0x00: "ExceptionList", 0x08: "StackBase", 0x10: "StackLimit", 0x18: "SubSystemTib",
       0x20: "FiberData", 0x28: "ArbitraryUserPointer", 0x30: "Self (TEB pointer)",
       0x40: "ProcessId", 0x48: "ThreadId", 0x58: "ThreadLocalStoragePointer (static TLS array)",
       0x60: "PEB pointer", 0x68: "LastErrorValue", 0x1478: "DeallocationStack"}


def teb_name(disp):
    if disp in TEB:
        return TEB[disp]
    if 0x1480 <= disp < 0x1480 + 64 * 8:
        return "TlsSlots[%d]" % ((disp - 0x1480) // 8)
    return "?"


def classify_handler(pe, begin, end, data_off):
    """Guess the handler kind from the language-specific data that follows its RVA."""
    d = pe.data
    if data_off + 4 > len(d):
        return "unknown"
    (first,) = struct.unpack_from("<I", d, data_off)
    off = pe.rva_to_off(first)
    if off is not None and off + 4 <= len(d):
        (magic,) = struct.unpack_from("<I", d, off)
        if 0x19930520 <= magic <= 0x19930522:
            return "C++ exception handling (FuncInfo)"
    if 0 < first <= 64 and data_off + 4 + 16 * first <= len(d):
        ok = True
        for i in range(first):
            b, e, h, t = struct.unpack_from("<IIII", d, data_off + 4 + 16 * i)
            if not (begin <= b <= e <= end + 16):
                ok = False
                break
        if ok:
            return "structured exception handling (scope table)"
    return "stack-cookie check or other"


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
    gs = Counter()
    handlers = defaultdict(Counter)   # handler rva -> kind -> functions
    for i in range(size // 12):
        begin, end, unwind = struct.unpack_from("<III", pe.data, off + 12 * i)
        uoff = pe.rva_to_off(unwind & ~1)
        if uoff is not None:
            ver_flags, _, ncodes, _ = struct.unpack_from("<BBBB", pe.data, uoff)
            flags = ver_flags >> 3
            if not flags & 4 and flags & 3:
                hoff = uoff + 4 + 2 * ((ncodes + 1) & ~1)
                (h,) = struct.unpack_from("<I", pe.data, hoff)
                handlers[h][classify_handler(pe, begin, end, hoff + 4)] += 1
        boff = pe.rva_to_off(begin)
        if boff is None:
            continue
        code = pe.data[boff:boff + (end - begin)]
        if b"\x65" not in code:      # gs segment override prefix
            continue
        for ins in md.disasm(code, begin):
            for op in ins.operands:
                if op.type == X.X86_OP_MEM and op.mem.segment == X.X86_REG_GS:
                    key = op.mem.disp if not (op.mem.base or op.mem.index) else "computed"
                    gs[key] += 1
    return os.path.basename(path), gs, handlers


def main():
    root = sys.argv[1]
    gs_total, kinds_total = Counter(), Counter()
    per_file = []
    for path in find_pe_files(root):
        if os.path.basename(path).lower() == "d3dcompiler_46.dll":
            continue
        r = scan(path)
        if not r:
            continue
        name, gs, handlers = r
        gs_total.update(gs)
        kinds = Counter()
        for h, c in handlers.items():
            kinds.update(c)
        kinds_total.update(kinds)
        per_file.append((name, len(handlers), dict(kinds)))

    print("## gs: operands by TEB offset\n\n| Offset | Field | Count |\n|---|---|---|")
    for disp, c in sorted(gs_total.items(), key=lambda kv: -kv[1]):
        if disp == "computed":
            print("| (register-based) | computed offset | %d |" % c)
        else:
            print("| 0x%x | %s | %d |" % (disp, teb_name(disp), c))
    print("\n## Functions with handlers, by kind (all modules)\n\n| Kind | Functions |\n|---|---|")
    for k, c in kinds_total.most_common():
        print("| %s | %d |" % (k, c))
    print("\n## Per module\n\n| Module | Distinct handler routines | C++ EH | SEH | Cookie/other |\n|---|---|---|---|---|")
    for name, n, kinds in sorted(per_file, key=lambda r: -sum(r[2].values())):
        print("| %s | %d | %d | %d | %d |" % (
            name, n, kinds.get("C++ exception handling (FuncInfo)", 0),
            kinds.get("structured exception handling (scope table)", 0),
            kinds.get("stack-cookie check or other", 0)))


if __name__ == "__main__":
    main()
