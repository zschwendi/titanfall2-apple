#!/usr/bin/env python3
"""Command-line switches a module reads, and which of them sit on a call path (finding 37, Q3).

    .venv/bin/python tools/scout/flow_switches.py MODULE                      # every switch string
    .venv/bin/python tools/scout/flow_switches.py MODULE --roots 0xA,0xB [--depth N] [--skip 0xC,...]

A switch is a string that starts with '-' or '+' followed by a letter and whose address a
function loads.  For each load the script looks at the next indirect call in the same function
and, when that call goes through a vtable slot, reports the slot of the command-line object
(tier0's CCommandLine: 4 check, 5 remove, 6 append, 7/8/9 value as float/int/text, 11 find;
slots 1 and 11 were read, the others are named from the shape of their calls).  With --roots,
only functions reached from the roots by direct calls and tail jumps (depth N, default 5) are
listed, in the order a depth-first walk in code order meets them.  Calls through vtables are
not followed.  Prints addresses, names and strings.  OriginSDK.dll is refused.
"""

import re
import sys

from flow_lib import get
from capstone import x86_const as X

SW = re.compile(r"^[-+][A-Za-z_][A-Za-z0-9_]*$")
SLOT = {4: "check", 5: "remove", 6: "append", 7: "float", 8: "int", 9: "text", 11: "find"}


def switches_of(M, f):
    out, pending = [], None
    for ins in M.insns(f):
        if ins.mnemonic == "lea":
            for op in ins.operands:
                if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                    s = M.cstr(ins.address + ins.size + op.mem.disp, 48)
                    if s and SW.match(s):
                        if pending:
                            out.append((pending[0], pending[1], "?"))
                        pending = (s, ins.address)
        elif ins.mnemonic in ("call", "jmp") and pending:
            ct = M.call_target(ins)
            if ct and ct[0] == "vslot":
                out.append((pending[0], pending[1], SLOT.get(ct[1][1] // 8, "slot %d" % (ct[1][1] // 8))))
                pending = None
            elif ct and ct[0] == "direct" and ins.mnemonic == "call":
                out.append((pending[0], pending[1], "passed to f%x" % ct[1]))
                pending = None
    if pending:
        out.append((pending[0], pending[1], "?"))
    return out


def reach(M, roots, depth, skip):
    order, seen = [], set()

    def rec(f, d):
        if f in seen or f in skip:
            return
        seen.add(f)
        order.append((f, d))
        if d >= depth:
            return
        for ins in M.insns(f):
            if ins.mnemonic in ("call", "jmp"):
                ct = M.call_target(ins)
                if ct and ct[0] == "direct":
                    t = M.func_of(ct[1]) if M.func_of(ct[1]) is not None else ct[1]
                    if t != f:
                        rec(t, d + 1)
    for r in roots:
        rec(r, 0)
    return order


def main():
    a = sys.argv[1:]
    M = get(a[0])
    if "--roots" in a:
        roots = [int(x, 16) for x in a[a.index("--roots") + 1].split(",")]
        depth = int(a[a.index("--depth") + 1]) if "--depth" in a else 5
        skip = set(int(x, 16) for x in a[a.index("--skip") + 1].split(",")) if "--skip" in a else set()
        order = reach(M, roots, depth, skip)
        n = 0
        for f, d in order:
            for s, site, how in switches_of(M, f):
                print("  %-22s %-18s %s+0x%x in %s (depth %d)" % (s, how, M.short, site, M.label(f), d))
                n += 1
        print("%s: %d function(s) reached, %d switch read(s)" % (M.short, len(order), n))
        return
    by = {}
    for site, tgt in M.m.lea_refs():
        s = M.cstr(tgt, 48)
        if s and SW.match(s) and not M.is_code(tgt):
            by.setdefault(s, set()).add(M.start_of(site) if M.func_of(site) is None else M.func_of(site))
    for s in sorted(by, key=str.lower):
        print("  %-26s %s" % (s, " ".join("+0x%x" % f for f in sorted(by[s]))))
    print("%s: %d switch string(s)" % (M.short, len(by)))


if __name__ == "__main__":
    main()
