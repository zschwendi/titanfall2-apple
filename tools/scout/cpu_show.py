#!/usr/bin/env python3
"""Print the instructions of one .pdata function (console only, for checking a scout result).

    .venv/bin/python tools/scout/cpu_show.py game/bin/x64_retail/tier0.dll 0xc740 [--around 0xc74d --window 14]

Shows the recursive-descent view of the function (reached instructions, in address order),
with '*' marking the address given by --around. Output is never written to a file.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import Dis, descend, function_table, load  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("func", help="function entry RVA (hex)")
    ap.add_argument("--around", help="RVA to mark")
    ap.add_argument("--window", type=int, default=0, help="only print this many instructions either side")
    ap.add_argument("--linear", action="store_true", help="linear sweep instead of recursive descent")
    args = ap.parse_args()
    pe = load(args.image)
    entries, funcs = function_table(pe)
    func = int(args.func, 16)
    ranges = funcs.get(func)
    if ranges is None:
        print("no .pdata function at", args.func)
        return
    dis = Dis(pe)
    if args.linear:
        insns = {i.rva: i for b, e in ranges for i in dis.linear(b, e)}
    else:
        insns, _, unresolved = descend(dis, ranges, [b for b, _ in ranges])
        print("unresolved indirect jumps:", ["%x" % u for u in unresolved])
    rvas = sorted(insns)
    marks = int(args.around, 16) if args.around else None
    if marks is not None and args.window and marks in insns:
        i = rvas.index(marks)
        rvas = rvas[max(0, i - args.window): i + args.window + 1]
    print("function ranges:", ["%x-%x" % (b, e) for b, e in ranges], " reached insns:", len(insns))
    for r in rvas:
        ins = insns[r]
        star = "*" if r == marks else " "
        print("%s %6x  %-10s %s" % (star, r, ins.mn, ins.text))


if __name__ == "__main__":
    main()
