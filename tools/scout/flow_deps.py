#!/usr/bin/env python3
"""Tables of (DLL name, interface name) pairs in a game DLL's data (finding 37).

    .venv/bin/python tools/scout/flow_deps.py MODULE [MODULE ...]

The launcher's "LOADING DEPENDENCIES" stage (launcher+0x14940) calls slot 6 of every system and
loads each pair of the table it returns.  This script finds such tables without knowing the
classes: any run of 16-byte entries whose first pointer is a string ending in ".dll" and whose
second is an identifier-like string.  For each table: its pairs, the functions that take its
address, and the vtables (RTTI class, slot) that hold those functions.  Prints addresses, names
and strings.  OriginSDK.dll is refused.
"""

import re
import struct
import sys

from flow_lib import get

DLL = re.compile(r"^[A-Za-z0-9_.]+\.dll$")
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def tables(M):
    base = M.m.base
    out = []
    for sec in M.m.data_sections():
        buf = M.m.data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
        n = len(buf) // 8
        vals = struct.unpack_from("<%dQ" % n, buf, 0)
        i = 0
        while i + 1 < n:
            a, b = vals[i], vals[i + 1]
            if base < a < base + 0x10000000 and base < b < base + 0x10000000:
                sa, sb = M.cstr(a - base, 64), M.cstr(b - base, 64)
                if sa and sb and DLL.match(sa) and IDENT.match(sb):
                    start = sec["va"] + 8 * i
                    pairs = []
                    while i + 1 < n:
                        a, b = vals[i], vals[i + 1]
                        if not (base < a < base + 0x10000000 and base < b < base + 0x10000000):
                            break
                        sa, sb = M.cstr(a - base, 64), M.cstr(b - base, 64)
                        if not (sa and sb and DLL.match(sa) and IDENT.match(sb)):
                            break
                        pairs.append((sa, sb))
                        i += 2
                    out.append((start, pairs))
                    continue
            i += 1
    return out


def holders(M, fn):
    res = []
    for p in M.m.abs_refs([fn]).get(fn, []):
        for back in range(0, 600):
            c = M.vtable_class(p - 8 * back)
            if c:
                res.append("%s slot %d" % (c, back))
                break
        else:
            res.append("table at +0x%x (no RTTI)" % p)
    return res


def main():
    for name in sys.argv[1:]:
        M = get(name)
        ts = tables(M)
        print("%s: %d table(s)" % (M.short, len(ts)))
        for start, pairs in ts:
            print("  +0x%x: %s" % (start, "; ".join("%s %s" % p for p in pairs)))
            for site, mn, fm in M.data_refs(start):
                f = M.start_of(site)
                print("      taken at +0x%x in +0x%x (%d insn)  held by: %s" % (
                    site, f, len(M.insns(f)), ", ".join(holders(M, f)) or "no data pointer"))


if __name__ == "__main__":
    main()
