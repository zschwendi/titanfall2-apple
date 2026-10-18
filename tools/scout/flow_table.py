#!/usr/bin/env python3
"""Read a run of 8-byte values in a module's data as a table (finding 37).

    .venv/bin/python tools/scout/flow_table.py MODULE 0xRVA COUNT

Each value is shown as what it points at: a string the image holds, a function (export name or
rva), an import slot, another data address, or a plain number.  Used for the tables a start-up
sequence walks (interface names with the globals they fill, lists of DLL names).  Prints
addresses, names and strings.  OriginSDK.dll is refused.
"""

import sys

from flow_lib import get, fmt_imp


def describe(M, v):
    base = M.m.base
    if v is None:
        return "(outside the file: zero at load)"
    if not (base <= v < base + 0x10000000):
        return "%#x" % v
    r = v - base
    if r in M.slots:
        return "import slot " + fmt_imp(M.slots[r])
    if M.is_code(r):
        return "code " + M.label(r)
    s = M.cstr(r)
    if s and len(s) >= 3:
        return "str %r" % s[:80]
    c = M.vtable_class(r)
    if c:
        return "vtable " + c
    return "data %s+0x%x" % (M.m.sec_name(r) or "?", r)


def main():
    M = get(sys.argv[1])
    rva = int(sys.argv[2], 16)
    n = int(sys.argv[3])
    for i in range(n):
        v = M.m.u64(rva + 8 * i)
        print("  +0x%-6x %s" % (rva + 8 * i, describe(M, v)))


if __name__ == "__main__":
    main()
