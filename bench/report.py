#!/usr/bin/env python3
"""Tables from the driver's JSON results (bench/README.md).

    .venv/bin/python bench/report.py summary bench/build/results.json
    .venv/bin/python bench/report.py functions bench/build/results.json        # markdown table, one row per function
    .venv/bin/python bench/report.py variants bench/build/v/*/results.json      # geometric means per kind and variant
    .venv/bin/python bench/report.py compare bench/build/v/O2/results.json bench/build/v/deadflags/results.json
    .venv/bin/python bench/report.py ordering off1.json on1.json off2.json on2.json   # x86 memory-ordering mode (bench/tso.sh)

The headline number leaves out kind `probe` (intrare.c, whose native side is a hand-written
reference rather than the same source).  Ratios are best round of the lifted side over best
round of the native side of the same run; with --median anywhere on the command line, median
round over median round.
"""

import json
import math
import sys

KINDS = ["arith", "mem", "call", "switch", "virtual", "atomic", "probe"]


def load(path):
    with open(path) as f:
        d = json.load(f)
    d["by"] = {(f["file"], f["name"]): f for f in d["functions"]}
    return d


def timed(d):
    return [f for f in d["functions"] if "native_ns" in f]


MEDIAN = False


def ratio(f):
    if MEDIAN:
        return f["lifted_med"] / f["native_med"]
    return f["lifted_ns"] / f["native_ns"]


def gmean(xs):
    xs = list(xs)
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


def summary(d, keys=None):
    """{kind or 'all': (n, geometric mean, min, max)}; 'all' leaves out probe."""
    out = {}
    fs = [f for f in timed(d) if keys is None or (f["file"], f["name"]) in keys]
    for k in KINDS + ["all"]:
        rs = [ratio(f) for f in fs if (f["kind"] == k if k != "all" else f["kind"] != "probe")]
        if rs:
            out[k] = (len(rs), gmean(rs), min(rs), max(rs))
    return out


def cmd_summary(paths):
    for p in paths:
        d = load(p)
        st = {}
        for f in d["functions"]:
            st[f["status"]] = st.get(f["status"], 0) + 1
        print("%s: %s" % (d["label"], ", ".join("%s %d" % kv for kv in sorted(st.items()))))
        for k, (n, g, lo, hi) in summary(d).items():
            print("  %-8s %3d functions  %5.2fx  (%.2fx to %.2fx)" % (k, n, g, lo, hi))
        sp_n = sorted((f["native_med"] - f["native_ns"]) / f["native_ns"] for f in timed(d))
        sp_l = sorted((f["lifted_med"] - f["lifted_ns"]) / f["lifted_ns"] for f in timed(d))
        if sp_n:
            q = lambda v, x: v[min(len(v) - 1, int(x * len(v)))]
            print("  spread (median round over best round - 1): native median %.1f%%, 90th percentile %.1f%%; "
                  "lifted median %.1f%%, 90th percentile %.1f%%" % (100 * q(sp_n, .5), 100 * q(sp_n, .9), 100 * q(sp_l, .5), 100 * q(sp_l, .9)))
            print("  measurements not on a performance core: %d" % d.get("measurements_not_on_a_performance_core", 0))


def cmd_functions(paths):
    d = load(paths[0])
    print("| File | Function | Kind | Native ns | Lifted ns | Ratio | Ratio of medians | Spread n / l |")
    print("|---|---|---|---:|---:|---:|---:|---:|")
    for f in d["functions"]:
        if "native_ns" in f:
            print("| %s | `%s` | %s | %.2f | %.2f | %.2f | %.2f | %.0f%% / %.0f%% |" % (
                f["file"], f["name"], f["kind"], f["native_ns"], f["lifted_ns"], f["lifted_ns"] / f["native_ns"],
                f["lifted_med"] / f["native_med"],
                100 * (f["native_med"] - f["native_ns"]) / f["native_ns"], 100 * (f["lifted_med"] - f["lifted_ns"]) / f["lifted_ns"]))
        else:
            note = f["status"] + (" (%s)" % f["explained"] if f.get("explained") else "")
            print("| %s | `%s` | %s | | | %s | | |" % (f["file"], f["name"], f["kind"], note))


def cmd_variants(paths):
    ds = [load(p) for p in paths]
    common = None
    for d in ds:
        keys = {(f["file"], f["name"]) for f in timed(d)}
        common = keys if common is None else common & keys
    print("| Variant | " + " | ".join(KINDS[:-1]) + " | all but probe | probe |")
    print("|---|" + "---:|" * (len(KINDS) + 1))
    for d in ds:
        s = summary(d, common)
        cell = lambda k: "%.2f" % s[k][1] if k in s else ""
        print("| %s | %s | **%s** | %s |" % (d["label"], " | ".join(cell(k) for k in KINDS[:-1]), cell("all"), cell("probe")))
    s = summary(ds[0], common)
    print("| functions | %s | %s | %s |" % (" | ".join(str(s[k][0]) if k in s else "" for k in KINDS[:-1]),
                                            s["all"][0] if "all" in s else "", s["probe"][0] if "probe" in s else ""))


def cmd_compare(paths):
    a, b = load(paths[0]), load(paths[1])
    rows = []
    for key, fa in a["by"].items():
        fb = b["by"].get(key)
        if fb and "native_ns" in fa and "native_ns" in fb:
            rows.append((ratio(fb) / ratio(fa), key, fa, fb))
    rows.sort()
    print("%-34s %-8s %8s %8s %8s" % ("function", "kind", a["label"], b["label"], "b/a"))
    for r, key, fa, fb in rows:
        print("%-34s %-8s %8.2f %8.2f %8.2f" % ("%s/%s" % key, fa["kind"], ratio(fa), ratio(fb), r))


def cmd_ordering(paths):
    """Cost of the x86 memory-ordering mode: runs alternate off, on, off, on.  Per function
    and side, the best time over the runs of each mode; then on / off."""
    runs = [load(p) for p in paths]
    off, on = runs[0::2], runs[1::2]

    def best(ds, key, field):
        vals = [d["by"][key][field] for d in ds if key in d["by"] and field in d["by"][key]]
        return min(vals) if vals else None

    rows = []
    for key, f in off[0]["by"].items():
        if "native_ns" not in f:
            continue
        v = [best(off, key, "lifted_ns"), best(on, key, "lifted_ns"), best(off, key, "native_ns"), best(on, key, "native_ns")]
        if None not in v:
            rows.append((key, f["kind"], v))
    print("| Kind | Functions | Lifted, on / off | Native, on / off | Lifted / native, off | Lifted / native, on |")
    print("|---|---:|---:|---:|---:|---:|")
    for k in KINDS + ["all"]:
        sel = [v for key, kind, v in rows if (kind == k if k != "all" else kind != "probe")]
        if sel:
            print("| %s | %d | %.3f | %.3f | %.2f | %.2f |" % (
                k if k != "all" else "all but probe", len(sel), gmean(v[1] / v[0] for v in sel), gmean(v[3] / v[2] for v in sel),
                gmean(v[0] / v[2] for v in sel), gmean(v[1] / v[3] for v in sel)))
    if len(off) > 1:
        keys = [key for key, kind, v in rows if "lifted_ns" in off[1]["by"].get(key, {})]
        print("\nrepeatability, second run of a mode over the first (geometric mean over %d functions): lifted off %.3f, lifted on %.3f, "
              "native off %.3f, native on %.3f" % (
                  len(keys), gmean(off[1]["by"][k]["lifted_ns"] / off[0]["by"][k]["lifted_ns"] for k in keys),
                  gmean(on[1]["by"][k]["lifted_ns"] / on[0]["by"][k]["lifted_ns"] for k in keys),
                  gmean(off[1]["by"][k]["native_ns"] / off[0]["by"][k]["native_ns"] for k in keys),
                  gmean(on[1]["by"][k]["native_ns"] / on[0]["by"][k]["native_ns"] for k in keys)))
    rows.sort(key=lambda r: -(r[2][1] / r[2][0]))
    print("\nmost affected lifted functions (on / off):")
    for key, kind, v in rows[:12]:
        print("  %-34s %-8s lifted %8.2f -> %8.2f ns (%.2f)   native %8.2f -> %8.2f ns (%.2f)" % (
            "%s/%s" % key, kind, v[0], v[1], v[1] / v[0], v[2], v[3], v[3] / v[2]))
    cores = {}
    for d in runs:
        for f in timed(d):
            cores[f.get("cores", "?")] = cores.get(f.get("cores", "?"), 0) + 1
    print("\ncore class of the timings over all runs: %s" % ", ".join("%s %d" % kv for kv in sorted(cores.items())))


def main():
    global MEDIAN
    if "--median" in sys.argv:
        MEDIAN = True
        sys.argv.remove("--median")
    if len(sys.argv) < 3 or sys.argv[1] not in ("summary", "functions", "variants", "compare", "ordering"):
        sys.stderr.write(__doc__)
        return 2
    {"summary": cmd_summary, "functions": cmd_functions, "variants": cmd_variants, "compare": cmd_compare,
     "ordering": cmd_ordering}[sys.argv[1]](sys.argv[2:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
