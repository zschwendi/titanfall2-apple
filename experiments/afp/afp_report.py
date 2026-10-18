#!/usr/bin/env python3
"""Condense out/results.json into the per-operation status used in docs/findings/11-float-mode.md."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
data = json.load(open(os.path.join(HERE, "out", "results.json")))
R = [r for r in data["results"] if "error" not in r]


def family(op):
    for pre in ("cmp",):
        if op.startswith(pre) and op[3:-2] in ("eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"):
            return "cmp" + op[-2:]
    return op


def status(rs):
    """rs: results of one (op, variant, mode) across configs -> (code, text)"""
    plain = [r for r in rs if not (r["daz"] or r["ftz"])]
    den = [r for r in rs if (r["daz"] or r["ftz"])]
    def cls(group):
        if not group:
            return None
        if all(r["bad"] == 0 for r in group):
            return "E"
        if all((r["bad_nonnan"] == 0) for r in group if r["bad"]) and all(r["bad_nonnan"] is not None for r in group if r["bad"]):
            return "N"            # differs, but only in NaN result lanes
        return "X"
    return cls(plain), cls(den), len(rs), sum(r["bad"] for r in rs)


by = {}
for r in R:
    by.setdefault((r["op"], r["variant"].split("__", 1)[1]), {}).setdefault(r["mode"], []).append(r)

if __name__ == "__main__":
    only = sys.argv[1:]
    print(f"{'op':14s} {'variant':14s}  " + "  ".join(f"{m:>16s}" for m in ("def", "ah", "ah_nep", "fiz")) + "     (plain cfgs / denormal cfgs; E exact, N NaN-lanes only differ, X differs)")
    for (op, var), modes in sorted(by.items()):
        if only and not any(op.startswith(o) or o == family(op) for o in only):
            continue
        cells = []
        for m in ("def", "ah", "ah_nep", "fiz"):
            rs = modes.get(m)
            if not rs:
                cells.append(f"{'-':>16s}")
                continue
            p, d, n, tot = status(rs)
            cells.append(f"{(p or '-') + '/' + (d or '-'):>8s}({n:2d}){'':4s}")
        print(f"{op:14s} {var:14s}  " + "  ".join(cells))
