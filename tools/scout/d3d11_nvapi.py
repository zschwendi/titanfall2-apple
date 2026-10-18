#!/usr/bin/env python3
"""Which statically linked NVAPI wrappers does materialsystem_dx11 call? (issue #31, Q5/Q6)

    .venv/bin/python tools/scout/d3d11_nvapi.py

materialsystem_dx11.dll loads `nvapi64.dll` itself and resolves functions through
`nvapi_QueryInterface` by numeric id.  The NVAPI SDK's static wrappers each call one common
resolver with their id.  This lists the wrappers that code outside the wrapper block calls and
the id each passes (a 32-bit constant; the id-to-name table is NVIDIA's and is not in the repo).
Prints addresses and ids only.
"""

import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scout_ctx  # noqa: E402
import d3d11_decode as D  # noqa: E402

RESOLVER_STRING = "nvapi64.dll"


def main():
    c = scout_ctx.Ctx("materialsystem_dx11")
    m = c.mod
    dec = D.Decoder(m)
    # the function that loads nvapi64.dll and the one that calls nvapi_QueryInterface
    loader = None
    for a, size, mn, tgt, fm in c.t["rip"]:
        if mn == "lea" and m.cstring(tgt) == RESOLVER_STRING:
            loader = m.unit_of(a)
            break
    if loader is None:
        print("nvapi64.dll string reference not found")
        return
    callers = c.callers(loader)
    wrappers = sorted({m.unit_of(a) for a, k, cf in callers})
    print("resolver 0x%x called from %d wrapper functions in 0x%x..0x%x" % (loader, len(wrappers), wrappers[0], wrappers[-1]))
    lo, hi = wrappers[0], wrappers[-1] + 0x40
    ids = {}
    for w in wrappers:
        f = dec.func(dec.func_of(w) or w)
        cons = []
        for a, sz, mn, ops, k in f.insns:
            if mn == "mov" and len(ops) == 2 and ops[0][0] == "r" and ops[0][1] == 1 and ops[0][2] == 32 \
                    and ops[1][0] == "i" and (ops[1][1] & 0xFFFFFFFF) > 0xFFFF:
                cons.append(ops[1][1] & 0xFFFFFFFF)
        if cons:
            ids[w] = cons
    # who outside the block calls a wrapper (direct calls, plus code addresses taken)
    out = collections.defaultdict(list)
    for w in wrappers:
        for a, k, cf in c.callers(w):
            if not (lo <= a < hi):
                out[w].append(a)
        for site, cf in c.g.taken.get(w, []):
            if not (lo <= site < hi):
                out[w].append(site)
    print("wrappers with an id found: %d of %d" % (len(ids), len(wrappers)))
    print("wrappers called from outside the block: %d" % len(out))
    for w in sorted(out):
        print("   wrapper 0x%x id %s called from %s" % (w, ("/".join("0x%08x" % x for x in ids[w][:3])) if w in ids else "?",
                                                       ", ".join("0x%x" % a for a in out[w][:4])))
    refs = 0
    for w in wrappers:
        if c.callers(w) or c.g.taken.get(w):
            refs += 1


if __name__ == "__main__":
    main()
