#!/usr/bin/env python3
"""Turn d3d11_scan.py output into the tables of docs/findings/31-d3d11-usage.md and its JSON copy.

    .venv/bin/python tools/scout/d3d11_report.py --scan DIR [--json OUT.json] [--update FILE.md]

DIR holds <module>.json files written by d3d11_scan.py.  With --update, every block in FILE.md
between `<!-- BEGIN gen:NAME -->` and `<!-- END gen:NAME -->` is replaced by the generated text
for NAME, so the numbers in the findings file are reproducible and the prose around them can be
edited by hand.  The JSON holds names, offsets and counts only.

Confidence of a call site (see the findings file, section 3):
  certain    the strict analysis (nothing carried across calls, only the function's own code and
             global cells) and the full analysis agree on one interface
  likely     only the full analysis types it, and it finds one interface
  ambiguous  the full analysis finds several interfaces; the method name is the same for all of
             them ("likely*") or differs ("guess")
A table row's confidence is the best confidence among its sites.
"""

import argparse
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3d11_vtables as V  # noqa: E402

PRIMARY = "materialsystem_dx11"
ORDER = {"certain": 0, "likely": 1, "likely*": 2, "guess": 3}


def load_scan(d, name):
    p = os.path.join(d, name + ".json")
    if not os.path.exists(p):
        return None
    j = json.load(open(p))
    j["full"] = {int(a): x for a, x in j["runs"]["full"]["sites"].items()}
    j["strict"] = {int(a): x for a, x in j["runs"]["strict"]["sites"].items()}
    return j


def classify(j):
    """{site: (confidence, [(iface, method, slot)])} for every typed COM call, plus untyped lists."""
    typed = {}
    mixed = []
    unresolved = []
    for a, x in j["full"].items():
        if x["kind"] == "unresolved":
            unresolved.append(x)
            continue
        if x["kind"] != "com":
            continue
        labels = [tuple(l) for l in x["labels"] if l[0] != "*"]
        if not labels:
            mixed.append(x)
            continue
        types = sorted({l[0] for l in labels})
        s = j["strict"].get(a)
        stypes = sorted({l[0] for l in s["labels"]}) if s and s["kind"] == "com" else []
        if len(types) == 1:
            conf = "certain" if stypes == types else "likely"
        else:
            conf = "likely*" if len({(l[1]) for l in labels}) == 1 else "guess"
        # a receiver that also holds pointers to the engine's own classes means a helper shared
        # by COM and non-COM objects: the interface label is only a candidate
        impure = any(not r.startswith("O:") for r in x["args"][0]) if x["args"] else False
        if impure and conf != "certain":
            conf = "guess"
        typed[a] = (conf, sorted(set(labels)), x)
    return typed, mixed, unresolved


def method_rows(typed):
    """{(iface, method, slot): {conf: n, 'sites': n, 'fns': set}}"""
    rows = {}
    for a, (conf, labels, x) in typed.items():
        for T, m, sl in labels:
            r = rows.setdefault((T, m, sl), {"certain": 0, "likely": 0, "likely*": 0, "guess": 0, "fns": set()})
            r[conf] += 1
            r["fns"].add(x["fn"])
    return rows


def row_conf(r):
    for c in ("certain", "likely", "likely*", "guess"):
        if r[c]:
            return c
    return "guess"


def sites_total(r):
    return r["certain"] + r["likely"] + r["likely*"] + r["guess"]


def gen_method_table(rows, ifaces, module):
    out = []
    out.append("| Interface | Method | Offset | Sites | certain / likely / amb. | Functions | Confidence |")
    out.append("|---|---|---|---|---|---|---|")
    for (T, m, sl), r in sorted(rows.items(), key=lambda kv: (kv[0][0], kv[0][2])):
        out.append("| %s | %s | 0x%x | %d | %d / %d / %d | %d | %s |" % (
            T, m, 8 * sl, sites_total(r), r["certain"], r["likely"], r["likely*"] + r["guess"], len(r["fns"]), row_conf(r)))
    return "\n".join(out)


def coverage(rows, ifaces):
    per = collections.defaultdict(dict)
    for (T, m, sl), r in rows.items():
        per[T][m] = (sl, r)
    return per


def gen_coverage_table(rows, ifaces):
    per = coverage(rows, ifaces)
    out = ["| Interface | Methods | Called | Never called |", "|---|---|---|---|"]
    for T in sorted(per):
        it = ifaces.get(T)
        total = len(it.methods) if it else 0
        called = per[T]
        never = [mm.name for mm in it.methods if mm.name not in called] if it else []
        out.append("| %s | %d | %d | %s |" % (T, total, len(called), ", ".join(never) if never else "none"))
    return "\n".join(out)


def gen_unresolved(unresolved, mixed, j):
    c = collections.Counter(x["disp"] for x in unresolved)
    top = ", ".join("0x%x:%d" % (d, n) for d, n in sorted(c.items(), key=lambda kv: -kv[1])[:14])
    return top


def gen_iid_table(j):
    out = ["| Call site (RVA) | In function | Call | Interface ID passed |", "|---|---|---|---|"]
    for a, x in sorted(j["full"].items()):
        if "iid" not in x:
            continue
        lab = x["labels"][0]
        callee = "%s::%s" % (lab[0], lab[1])
        out.append("| 0x%x | 0x%x | %s | %s |" % (a, x["fn"], callee, x["iid"][1] or "unknown (" + x["iid"][0] + ")"))
    return "\n".join(out)


def build_json(scans, ifaces):
    out = {"schema": 1, "unit": "scout-d3d11-usage",
           "note": "names, offsets and counts only; confidence per table row is the best among its sites",
           "modules": {}}
    for name, j in scans.items():
        typed, mixed, unresolved = classify(j)
        rows = method_rows(typed)
        per = coverage(rows, ifaces)
        mod = {"notes": j.get("notes", []),
               "vtable_call_sites": {"typed": len(typed), "mixed_or_out_of_range": len(mixed), "unresolved": len(unresolved)},
               "interfaces": {}, "unresolved_by_offset": {}}
        for T in sorted(per):
            it = ifaces.get(T)
            mod["interfaces"][T] = {
                "methods_total": len(it.methods) if it else None,
                "methods_called": len(per[T]),
                "called": [{"method": m, "slot": sl, "offset": 8 * sl, "sites": sites_total(r),
                            "certain": r["certain"], "likely": r["likely"], "ambiguous": r["likely*"] + r["guess"],
                            "functions": len(r["fns"]), "confidence": row_conf(r)}
                           for m, (sl, r) in sorted(per[T].items(), key=lambda kv: kv[1][0])],
                "never_called": [mm.name for mm in it.methods if mm.name not in per[T]] if it else [],
            }
        c = collections.Counter(x["disp"] for x in unresolved)
        mod["unresolved_by_offset"] = {"0x%x" % d: n for d, n in sorted(c.items())}
        imps = collections.Counter()
        for x in j["full"].values():
            if x["kind"] == "import" and x["labels"][0][0] != "?":
                lab = x["labels"][0]
                if len(lab) > 2 or True:
                    imps["%s!%s" % (lab[0], lab[1])] += 1
        mod["import_call_sites"] = dict(sorted(imps.items()))
        out["modules"][name] = mod
    return out


def update_md(path, blocks):
    text = open(path).read()
    for name, body in blocks.items():
        pat = re.compile(r"<!-- BEGIN gen:%s -->.*?<!-- END gen:%s -->" % (re.escape(name), re.escape(name)), re.S)
        if not pat.search(text):
            continue
        repl = "<!-- BEGIN gen:%s -->\n%s\n<!-- END gen:%s -->" % (name, body, name)
        text = pat.sub(lambda m: repl, text)
    open(path, "w").write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", required=True)
    ap.add_argument("--json")
    ap.add_argument("--update")
    ap.add_argument("--print", action="store_true")
    a = ap.parse_args()
    ifaces = V.load()
    names = [PRIMARY, "GFSDK_SSAO.win64", "amd_ags_x64", "bink2w64", "GFSDK_TXAA.win64"]
    scans = {}
    for n in names:
        j = load_scan(a.scan, n)
        if j:
            scans[n] = j
    blocks = {}
    for n, j in scans.items():
        typed, mixed, unresolved = classify(j)
        rows = method_rows(typed)
        key = "main" if n == PRIMARY else n
        blocks["methods-" + key] = gen_method_table(rows, ifaces, n)
        blocks["coverage-" + key] = gen_coverage_table(rows, ifaces)
        blocks["unresolved-" + key] = gen_unresolved(unresolved, mixed, j)
        blocks["iids-" + key] = gen_iid_table(j)
        if a.print:
            print("== %s: typed %d, mixed %d, unresolved %d" % (n, len(typed), len(mixed), len(unresolved)))
            cc = collections.Counter(c for c, l, x in typed.values())
            print("   confidence of sites:", dict(cc))
    if a.json:
        js = build_json(scans, ifaces)
        with open(a.json, "w") as f:
            json.dump(js, f, indent=1, sort_keys=False)
            f.write("\n")
    if a.update:
        update_md(a.update, blocks)
    if a.print:
        for k, v in blocks.items():
            if k.startswith("methods-main") or k.startswith("coverage-main"):
                print("\n## " + k + "\n" + v)


if __name__ == "__main__":
    main()
