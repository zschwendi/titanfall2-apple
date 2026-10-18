#!/usr/bin/env python3
"""For each game DLL the original loaded before the first campaign map, count how many of
its external imports have a shim in runtime/shims/ today, by library.

Reads inventory/inventory.json (import tables), runtime/tools/imports.def (the marks of
finding 24) and the names and RT-STATUS tags of the files under runtime/shims/.  It opens
no game file.  Read only, single process, standard library.

    .venv/bin/python tools/scout/boot_import_gap.py                 # the tables
    .venv/bin/python tools/scout/boot_import_gap.py --through C     # by-library table up to engine
    .venv/bin/python tools/scout/boot_import_gap.py --missing user32.dll   # names without a shim

The load order is the one of docs/findings/30-proton-trace.md, section 3 (one traced run of
the original under Proton).  "Shim" means a file whose RT-STATUS is not `trap`; a trap-only
shim stops the run like a missing one and is counted as missing.  Marks (finding 24, judged
from names and importers, not observed): T tool or debug path, U looks unused in
single-player, N delay-loaded from a DLL the install does not contain, S imported only by
the stubbed NVIDIA DLLs, O imported only by OriginSDK.dll.
"""

import argparse
import json
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")

# Finding 30, section 3: order of first load in the traced run.  Left out on purpose:
# Titanfall2.exe and Core\Activation64.dll (the wrapper, replaced by our own entry point),
# OriginSDK.dll (not lifted: stop point 2, issue 32), d3dcompiler_46.dll (Microsoft's),
# ui(11).dll (imports nothing).  amd_ags_x64.dll was Proton's builtin in that run; the
# game's own file is listed because Windows would have loaded it.
STAGES = [
    ("A. launcher hands over", ["tier0.dll", "vstdlib.dll", "launcher.dll"]),
    ("B. pak and file system", ["rtech_game.dll", "filesystem_stdio.dll"]),
    ("C. engine", ["engine.dll"]),
    ("D. engine's subsystems", ["bink2w64.dll", "inputsystem.dll", "vphysics.dll",
                                "GFSDK_SSAO.win64.dll", "amd_ags_x64.dll",
                                "materialsystem_dx11.dll", "datacache.dll", "studiorender.dll",
                                "vguimatsurface.dll", "vgui2.dll", "localize.dll"]),
    ("E. sound, video, game code", ["mileswin64.dll", "binkawin64.dll", "client.dll",
                                    "server.dll"]),
]
NOT_LIFTED = {"originsdk.dll", "d3dcompiler_46.dll"}
STUBBED = {"GFSDK_SSAO.win64.dll"}          # PLAN.md: stubbed, not lifted


def shim_dir(lib):
    return re.sub(r"[^a-z0-9]", "_", lib.lower()[:-4] if lib.lower().endswith(".dll") else lib.lower())


def load_shims():
    """lib dir -> {name or 'ord<n>': status}"""
    out = defaultdict(dict)
    base = os.path.join(ROOT, "runtime", "shims")
    for d in sorted(os.listdir(base)):
        p = os.path.join(base, d)
        if not os.path.isdir(p):
            continue
        for f in os.listdir(p):
            if not f.endswith(".c"):
                continue
            head = open(os.path.join(p, f), errors="replace").read(600)
            m = re.search(r"RT-STATUS:\s*(\w+)", head)
            status = m.group(1) if m else "?"
            name = f[:-2]
            mo = re.match(r"ord(\d+)_", name)
            out[d][("#" + mo.group(1)) if mo else name] = status
    return out


def load_marks():
    marks = {}
    for line in open(os.path.join(ROOT, "runtime", "tools", "imports.def")):
        if line.startswith("#") or not line.strip():
            continue
        lib, name, ordinal, _wave, mark = line.split()[:5]
        marks[(lib, name)] = "" if mark == "-" else mark
        if ordinal != "-":
            marks[(lib, "#" + ordinal)] = "" if mark == "-" else mark
    return marks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inventory", default=os.path.join(ROOT, "inventory", "inventory.json"))
    ap.add_argument("--missing", metavar="LIB", help="list the names of LIB that have no shim")
    ap.add_argument("--through", metavar="STAGE", default="E",
                    help="letter of the last stage the by-library table covers (default E)")
    args = ap.parse_args()

    inv = json.load(open(args.inventory))
    mods = {f["file"]: f for f in inv["files"] if f["machine"] == "x86-64"
            and (f["rel"].startswith("bin/x64_retail/") or f["rel"] == "server.dll")}
    own = {m.lower() for m in mods}
    shims = load_shims()
    marks = load_marks()

    def imports(mod):
        """[(lib, name, delay)] external only; and the count bound to other game DLLs"""
        ext, internal, unlifted = [], 0, 0
        for kind in ("imports", "delay_imports"):
            for lib, fns in mods[mod][kind].items():
                l = lib.lower()
                if l in NOT_LIFTED:
                    unlifted += len(fns)
                elif l in own:
                    internal += len(fns)
                else:
                    ext += [(l, fn) for fn in fns]
        return ext, internal, unlifted

    def state(lib, fn):
        s = shims.get(shim_dir(lib), {}).get(fn)
        return "shim" if s and s != "trap" else "missing"

    print("Shims on disk: " + ", ".join("%s %d (%d trap-only)" % (
        d, len(v), sum(1 for s in v.values() if s == "trap")) for d, v in sorted(shims.items())))
    print()
    print("%-26s %5s %5s %5s %5s | %5s %5s" % ("module", "ext", "shim", "miss", "plain",
                                              "game", "unlft"))
    seen = {}
    stage_rows = []
    for title, names in STAGES:
        print("-- " + title)
        for mod in names:
            ext, internal, unlifted = imports(mod)
            have = sum(1 for l, f in ext if state(l, f) == "shim")
            miss = [(l, f) for l, f in ext if state(l, f) != "shim"]
            plain = sum(1 for k in miss if not marks.get(k, ""))
            note = "  (stubbed, not lifted: its imports need no shim)" if mod in STUBBED else ""
            print("%-26s %5d %5d %5d %5d | %5d %5d%s" % (mod, len(ext), have, len(miss), plain,
                                                        internal, unlifted, note))
            if mod not in STUBBED:
                for k in ext:
                    seen.setdefault(k, title)
        tot = len(seen)
        have = sum(1 for l, f in seen if state(l, f) == "shim")
        plain = sum(1 for k in seen if state(*k) != "shim" and not marks.get(k, ""))
        stage_rows.append((title, tot, have, tot - have, plain))
    print()
    print("ext: distinct imports from Windows libraries; shim: have a shim today; miss: none")
    print("(or trap-only); plain: missing and carrying no mark of finding 24; game: bound to")
    print("another lifted game DLL; unlft: from OriginSDK.dll or d3dcompiler_46.dll.")
    print()
    print("Cumulative, distinct imports of every lifted module loaded so far:")
    print("%-28s %5s %5s %5s %5s" % ("up to and including", "ext", "shim", "miss", "plain"))
    for r in stage_rows:
        print("%-28s %5d %5d %5d %5d" % r)

    print()
    seen = {k: v for k, v in seen.items() if v[0] <= args.through.upper()}
    print("By library, stages A to %s (distinct imports of the lifted modules loaded by then):"
          % args.through.upper())
    print("%-14s %5s %5s %5s %5s  %s" % ("library", "ext", "shim", "miss", "plain",
                                        "marks among the missing"))
    by = defaultdict(list)
    for k in seen:
        by[k[0]].append(k)
    for lib, ks in sorted(by.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        miss = [k for k in ks if state(*k) != "shim"]
        mk = defaultdict(int)
        for k in miss:
            for c in marks.get(k, ""):
                mk[c] += 1
        print("%-14s %5d %5d %5d %5d  %s" % (
            lib, len(ks), len(ks) - len(miss), len(miss),
            sum(1 for k in miss if not marks.get(k, "")),
            " ".join("%s%d" % kv for kv in sorted(mk.items()))))
    tot = len(seen)
    miss = [k for k in seen if state(*k) != "shim"]
    print("%-14s %5d %5d %5d %5d" % ("total", tot, tot - len(miss), len(miss),
                                    sum(1 for k in miss if not marks.get(k, ""))))

    if args.missing:
        lib = args.missing.lower()
        print()
        print("Missing in %s (name, mark, first stage that needs it):" % lib)
        for k in sorted((k for k in seen if k[0] == lib and state(*k) != "shim"),
                        key=lambda k: (seen[k], k[1].lower())):
            print("  %-34s %-3s %s" % (k[1], marks.get(k, "") or "-", seen[k][:2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
