#!/usr/bin/env python3
"""List every instruction that reads, writes or takes the address of given globals (issue #31).

    .venv/bin/python tools/scout/d3d11_globals.py MODULE RVA [RVA ...]

For the globals that hold the device, the immediate context, the DXGI objects and the swap chain
this shows who writes them, so that "the only writers are the creation call and a null store"
can be checked rather than assumed.  A store of an immediate zero is `null store`; a store from a
register is `store reg`; `lea` means the address is handed to a callee (an out-parameter).
Addresses and counts only.
"""

import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scout_ctx  # noqa: E402
import d3d11_decode as D  # noqa: E402


def main():
    mod = sys.argv[1]
    want = [int(x, 16) for x in sys.argv[2:]]
    c = scout_ctx.Ctx(mod)
    dec = D.Decoder(c.mod)
    # decode the functions that mention the globals, to see the source operand of each store
    by_g = collections.defaultdict(list)
    for a, size, mn, tgt, fm in c.t["rip"]:
        if tgt in want:
            by_g[tgt].append((a, mn, fm))
    for g in want:
        kinds = collections.Counter()
        writers = []
        for a, mn, fm in sorted(by_g[g]):
            fn = dec.func_of(a)
            f = dec.func(fn) if fn is not None else None
            kind = "other"
            if f and a in f.addr_index:
                ins = f.insns[f.addr_index[a]]
                ops = ins[3]
                if ins[2] == "mov" and len(ops) == 2 and ops[0][0] == "m" and ops[0][1] == -2:
                    kind = "null store" if (ops[1][0] == "i" and ops[1][1] == 0) else ("store reg" if ops[1][0] == "r" else "store other")
                elif ins[2] == "mov" and len(ops) == 2 and ops[1][0] == "m" and ops[1][1] == -2:
                    kind = "load"
                elif ins[2] == "lea":
                    kind = "lea (address passed)"
                elif ins[2] in ("cmp", "test"):
                    kind = "compare"
            kinds[kind] += 1
            if kind in ("null store", "store reg", "store other", "lea (address passed)"):
                writers.append("0x%x %s in 0x%x" % (a, kind, fn or 0))
        print("global 0x%x: %d references: %s" % (g, len(by_g[g]), dict(kinds)))
        for w in writers:
            print("      " + w)


if __name__ == "__main__":
    main()
