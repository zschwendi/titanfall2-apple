#!/usr/bin/env python3
"""Print one function (all its unwind pieces) with the interface labels the flow analysis found.

    .venv/bin/python tools/scout/d3d11_show.py MODULE FUNC_RVA [RESULTS.json]

Terminal use only; the output holds instruction text and is not meant to be committed.  With a
results file from d3d11_scan.py, each vtable call is followed by `; Interface::Method` (or
`; ? unresolved`).
"""

import json
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3d11_decode as D  # noqa: E402
import scout_ctx  # noqa: E402
import capstone  # noqa: E402


def main():
    mod = sys.argv[1]
    fn = int(sys.argv[2], 16)
    m = D.load_module(scout_ctx.module_path(mod))
    dec = D.Decoder(m)
    base = dec.func_of(fn) or fn
    ranges = sorted(dec.ranges_of(base) or [(fn, fn + 0x200)])
    sites = {}
    if len(sys.argv) > 3:
        d = json.load(open(sys.argv[3]))
        sites = {int(a): x for a, x in d["runs"]["full"]["sites"].items()}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for b, e in ranges:
        print("; range %x-%x" % (b, e))
        buf = m.read(b, e - b)
        pos = 0
        while pos < len(buf):
            got = False
            for i in md.disasm(buf[pos:], b + pos):
                got = True
                pos = i.address - b + i.size
                note = ""
                x = sites.get(i.address)
                if x:
                    if x["kind"] == "com":
                        note = "   ; " + " | ".join("%s::%s" % (l[0], l[1]) for l in x["labels"]) if x["labels"] else "   ; com (offset outside the interface)"
                    elif x["kind"] == "unresolved":
                        note = "   ; ? unresolved"
                    else:
                        note = "   ; import " + str(x["labels"][0][1])
                print("  %6x  %-8s %s%s" % (i.address, i.mnemonic, i.op_str, note))
            if pos < len(buf) and not got:
                pos += 1


if __name__ == "__main__":
    main()
