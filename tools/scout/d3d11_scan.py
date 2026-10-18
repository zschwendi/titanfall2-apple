#!/usr/bin/env python3
"""Run the COM pointer-flow analysis on one module and write the raw results (issue #31).

    .venv/bin/python tools/scout/d3d11_scan.py MODULE [--out FILE.json] [--seed-ssao]

Runs the strict analysis (nothing carried across calls) and the full one, then records every
indirect call through a vtable with the interface labels each run found.  The JSON holds
addresses, interface names, method names and counts only.  d3d11_report.py turns it into the
tables in docs/findings/31-d3d11-usage.md.
"""

import argparse
import collections
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3d11_flow as F  # noqa: E402
import scout_ctx  # noqa: E402
import xref_lib  # noqa: E402


def seeds_for(prog, module):
    """Entry points whose arguments come from outside the module, from public SDK signatures."""
    seeds = {}
    notes = []
    if module.lower().startswith("gfsdk_ssao"):
        ex = xref_lib.export_rvas(prog.mod.pe)
        fn = ex.get("GFSDK_SSAO_CreateContext_D3D11")
        if fn is not None:
            seeds[fn] = {0: frozenset({("O", "ID3D11Device")})}
            notes.append("GFSDK_SSAO_CreateContext_D3D11 arg0 = ID3D11Device* (public NVIDIA HBAO+ signature)")
    if module.lower().startswith("amd_ags"):
        ex = xref_lib.export_rvas(prog.mod.pe)
        fn = ex.get("agsDriverExtensionsDX11_Init")
        if fn is not None:
            seeds[fn] = {1: frozenset({("O", "ID3D11Device")})}
            notes.append("agsDriverExtensionsDX11_Init arg1 = ID3D11Device* (AGS signature; the game's call at "
                         "materialsystem_dx11 0x11c58 passes the device there)")
    return seeds, notes


def run(module, quiet=False):
    p = F.Prog(scout_ctx.module_path(module), quiet=quiet)
    seeds, notes = seeds_for(p, module)
    out = {"module": p.name, "notes": notes, "runs": {}}
    t0 = time.time()
    solvers = {}
    for strict in (True, False):
        s = F.Solver(p, strict=strict, seeds=seeds, quiet=True)
        rounds = s.run()
        recs = s.record_pass()
        sites = {}
        for fn, lst in recs.items():
            for x in lst:
                sites[x["addr"]] = x
        out["runs"]["strict" if strict else "full"] = {
            "rounds": rounds,
            "seconds": round(time.time() - t0, 1),
            "stats": dict(s.stats),
            "overflow": s.overflow,
            "unions": len(s.unions),
            "sites": sites,
        }
        solvers["strict" if strict else "full"] = s
    return p, solvers, out


def holders(solver, prog):
    """Memory cells that ever hold a COM object pointer: [(region, offset, {iface: [store sites]})]."""
    out = []
    for base, cells in solver.cells.items():
        for off, atoms in cells.items():
            types = sorted(a[1] for a in atoms if a[0] == "O")
            if types:
                ss = solver.store_sites.get((base, off), {})
                out.append({"base": repr(base), "off": off, "types": types,
                            "stores": {t: sorted(ss.get(t, ())) for t in types}})
    return out


def returners(solver, prog):
    """Functions whose return value can be a COM object pointer, with the RTTI vtable slots that
    list them (a slot means another module could reach the function through that class)."""
    out = []
    for fn, atoms in solver.ret.items():
        types = sorted({a[1] for a in atoms if a[0] == "O"})
        if types:
            slots = ["%s#%d" % (xref_lib.demangle_class(c), sl) for c, sl in prog.vslots.get(fn, [])]
            out.append({"fn": fn, "types": types, "vtable_slots": slots,
                        "callers": len(solver.callers.get(fn, ()))})
    return sorted(out, key=lambda r: r["fn"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("--out")
    a = ap.parse_args()
    p, solvers, out = run(a.module)
    out["holders"] = holders(solvers["full"], p)
    out["returners"] = returners(solvers["full"], p)
    if a.out:
        with open(a.out, "w") as f:
            json.dump(out, f)
        print("wrote", a.out, file=sys.stderr)
    for k, r in out["runs"].items():
        c = collections.Counter(x["kind"] for x in r["sites"].values())
        print(k, "rounds", r["rounds"], dict(c), "overflow", r["overflow"], "unions", r["unions"])


if __name__ == "__main__":
    main()
