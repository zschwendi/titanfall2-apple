#!/usr/bin/env python3
"""Export inventory of tier0.dll and vstdlib.dll (finding 21, section 1).

Reads each DLL's export directory, demangles the C++ names (gate2_demangle.py), groups the exports by
subsystem (gate2_lib.subsystem), joins the lifter's report (status, instruction count) and the direct-call
closure (gate2_lib.Mod.closure), and prints a summary.  `build()` returns the records used by gate2_report.py.

    .venv/bin/python tools/scout/gate2_exports.py
"""

import collections
import sys

import gate2_lib as L

STATUS_CODE = {"lifted": "L", "skipped": "S", "lifted-without-handlers": "W", "not-in-report": "?"}


def short_args(d):
    if d.get("args") is None:
        return None
    return list(d["args"])


def record(M, e):
    d = e["dem"]
    rec = {
        "module": M.name, "ordinal": e["ordinal"], "name": e["name"], "text": d["text"], "demangled_ok": d["ok"],
        "mangled": e["name"].startswith("?"), "section": e["section"], "rva": e["rva"], "kind": e["kind"],
        "subsystem": L.subsystem(e["name"], d), "scope": d.get("scope"), "member": d.get("member"),
        "access": d.get("access"), "static": d.get("static"), "virtual": d.get("virtual"), "ret": d.get("ret"),
        "args": short_args(d), "variadic": d.get("variadic"),
    }
    if e["kind"] == "function":
        r = M.rep(e["rva"])
        rec["lifter"] = M.status(e["rva"])
        rec["insns"] = r["insns"] if r else None
        rec["skip_reasons"] = sorted({x["what"] for x in r.get("reasons", [])}) if r else []
        c = M.closure(e["rva"])
        rec["closure_units"] = len(c["units"])
        rec["closure_insns"] = c["insns"]
        rec["closure_skipped"] = ["%#x" % u for u in c["skipped"]]
        rec["indirect_sites"] = c["indirect"]
        rec["imports"] = sorted({"%s!%s" % k for k in c["imports"]})
        rec["globals"] = len(c["globals_read"] | c["globals_written"])
    return rec


def build():
    inv = {}
    for mn in L.MODULES:
        M = L.load(mn)
        recs = [record(M, e) for e in sorted(M.exports, key=lambda x: x["ordinal"])]
        fn = [r for r in recs if r["kind"] == "function"]
        inv[mn] = {
            "records": recs,
            "count": len(recs), "functions": len(fn), "data": len(recs) - len(fn),
            "mangled": sum(1 for r in recs if r["mangled"]), "plain_c": sum(1 for r in recs if not r["mangled"]),
            "forwarders": sum(1 for e in M.exports if e["forwarder"]),
            "demangle_failures": sum(1 for r in recs if not r["demangled_ok"]),
            "lifter": dict(collections.Counter(r["lifter"] for r in fn)),
            "image_base": M.pe.image_base, "entry_rva": M.pe.entry_rva,
            "timestamp": M.pe.timestamp,
            "sections": [(s["name"], s["va"], s["vsize"]) for s in M.pe.sections],
        }
    return inv


def md_tables(inv, mn):
    """Markdown tables, one per subsystem, for one module."""
    groups = collections.defaultdict(list)
    for r in inv[mn]["records"]:
        groups[r["subsystem"]].append(r)
    out = []
    for g in sorted(groups, key=lambda k: (-len(groups[k]), k)):
        rows = groups[g]
        out.append("**%s** (%d)\n" % (g, len(rows)))
        out.append("| Ord | Export (demangled) | RVA | Kind | Lifter | Insns | Types from |")
        out.append("|---|---|---|---|---|---|---|")
        for r in sorted(rows, key=lambda x: x["ordinal"]):
            if r["kind"] == "function":
                basis = "mangled name" if r["mangled"] and r["demangled_ok"] else "none (C name)"
                out.append("| %d | `%s` | %#x | fn | %s | %s | %s |" % (
                    r["ordinal"], disp(r["text"]), r["rva"], L_code(r), r["insns"], basis))
            else:
                out.append("| %d | `%s` | %#x | data (%s) | | | %s |" % (
                    r["ordinal"], disp(r["text"]), r["rva"], r["section"],
                    "mangled name" if r["mangled"] else "none (C name)"))
        out.append("")
    return "\n".join(out)


def disp(text):
    """Text for a markdown code span: the demangler's quoted names contain a backtick."""
    return text.replace("`", "'").replace("|", "\\|")


def L_code(r):
    c = STATUS_CODE.get(r["lifter"], "?")
    if c == "S" and r.get("skip_reasons"):
        c += " (%s)" % ",".join(r["skip_reasons"][:3])
    return c


def main():
    inv = build()
    for mn in L.MODULES:
        i = inv[mn]
        print("%s: %d exports (%d functions, %d data); %d mangled, %d plain C; %d forwarders; demangle failures %d; lifter %s" % (
            mn, i["count"], i["functions"], i["data"], i["mangled"], i["plain_c"], i["forwarders"], i["demangle_failures"], i["lifter"]))
        c = collections.Counter(r["subsystem"] for r in i["records"])
        for k, v in sorted(c.items(), key=lambda kv: -kv[1]):
            print("   %-45s %d" % (k, v))


if __name__ == "__main__":
    main()
