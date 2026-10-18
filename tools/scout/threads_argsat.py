#!/usr/bin/env python3
"""Show the few instructions before each call to a named import (issue #29). Terminal use.

    .venv/bin/python tools/scout/threads_argsat.py MODULE IMPORT_NAME_REGEX [N_BEFORE]
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module  # noqa: E402
import capstone  # noqa: E402


def main():
    m = Module(sys.argv[1])
    rx = re.compile(sys.argv[2])
    nb = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    slots = {s: (d, n) for s, (d, n) in m.import_slots().items() if rx.search(n)}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for site, kind, slot in m.iat_refs(slots.keys()):
        ent = m.entry_of(site)
        if not ent:
            print("site %x: no function" % site)
            continue
        code = m.bytes_at(ent[0], ent[1] - ent[0])
        ins = list(md.disasm(code, ent[0]))
        i = next((k for k, x in enumerate(ins) if x.address == site), None)
        print("-- %s site %x (f%x) %s" % (slots[slot][1][:40], site, ent[2], kind))
        if i is None:
            continue
        for x in ins[max(0, i - nb):i + 1]:
            print("     %7x %-6s %s" % (x.address, x.mnemonic, x.op_str))


if __name__ == "__main__":
    main()
