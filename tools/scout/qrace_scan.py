#!/usr/bin/env python3
"""The queue-race survey (finding 36): every critical region of every surveyed DLL, searched
for a hand-made compare-and-swap that reads the swapped location twice.

    .venv/bin/python tools/scout/qrace_scan.py OUT_DIR [module.dll ...]

One JSON file per module in OUT_DIR (addresses, offsets, classes, counts) and one summary
line per module on the screen. Up to 4 worker processes. The shape, the classes and the
walk are described in qrace_lib.py; OriginSDK.dll and d3dcompiler_46.dll are never opened.
"""
import json
import multiprocessing as mp
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qrace_lib as Q  # noqa: E402


def work(args):
    name, out = args
    t = time.time()
    ctx = Q.Ctx(name)
    fns = ctx.lock_functions()
    res = {"module": name, "lock_imports": sorted(set(ctx.slot_names.values())),
           "wrappers": {"0x%x" % f: k for f, k in sorted(ctx.wrappers.items())},
           "functions_with_lock_call": len(fns), "enter_sites": 0, "truncated_regions": 0,
           "functions_without_enter_site": 0, "single_read_cas_stores": 0, "hits": [], "errors": [],
           "unrecognised_enter": []}
    singles = []
    for f in fns:
        try:
            r = ctx.scan_function(f)
        except Exception as e:  # keep the survey going; the function is listed as not searched
            res["errors"].append(["0x%x" % f, repr(e)[:120]])
            continue
        res["enter_sites"] += r["enters"]
        res["truncated_regions"] += r["truncated"]
        if not r["enters"]:
            res["functions_without_enter_site"] += 1
            how = sorted(k for k, lk in ctx.ref_kinds.get(f, ()) if lk in ("enter", "tryenter"))
            if how:  # it uses an acquiring import, but no acquiring call was recognised
                res["unrecognised_enter"].append(["0x%x" % f, "+".join(how)])
        singles.extend(r["single_read_cas"])
        for h in r["hits"]:
            h = dict(h)
            for k in ("fn", "enter", "read1", "read2", "cmp", "jcc", "store"):
                h[k] = "0x%x" % h[k]
            h["fate"] = [[a, "0x%x" % b, c] for a, b, c in h["fate"]]
            res["hits"].append(h)
    res["single_read_cas_stores"] = len(singles)
    res["single_read_cas"] = ["0x%x" % a for a in sorted(singles)]
    res["seconds"] = round(time.time() - t, 1)
    with open(os.path.join(out, Q.short(name) + ".json"), "w") as fh:
        json.dump(res, fh, indent=1)
    return res


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    names = sys.argv[2:] or Q.survey_modules()
    with mp.Pool(min(4, len(names))) as pool:
        for r in pool.imap_unordered(work, [(n, out) for n in names]):
            a = sum(1 for h in r["hits"] if h["class"] == "A")
            print("%-26s lock fns %5d  enters %5d  trunc %4d  errors %3d  single-read %4d  hits A %3d  S %3d  %6.1fs"
                  % (r["module"], r["functions_with_lock_call"], r["enter_sites"], r["truncated_regions"],
                     len(r["errors"]), r["single_read_cas_stores"], a, len(r["hits"]) - a, r["seconds"]),
                  flush=True)


if __name__ == "__main__":
    main()
