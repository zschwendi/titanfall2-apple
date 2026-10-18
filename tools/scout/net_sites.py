#!/usr/bin/env python3
"""Where the boot-path modules call the socket libraries (unit rt-contract-net).

    .venv/bin/python tools/scout/net_sites.py                      # counts per module and function
    .venv/bin/python tools/scout/net_sites.py engine.dll           # every call site of one module
    .venv/bin/python tools/scout/net_sites.py engine.dll --fn      # grouped by calling function,
                                                                   # with the strings each one loads
    .venv/bin/python tools/scout/net_sites.py engine.dll --args 8 --only socket,bind
                                                                   # the 8 instructions before each call

Reads the import tables and code of launcher, engine, client and server (never OriginSDK.dll,
never the executable) through threads_lib.  An import by ordinal is named from
runtime/tools/imports.def.  A call site is `call [slot]`, `jmp [slot]` (a thunk: its direct
callers are then listed as the sites), or a `mov reg, [slot]` load (printed as `load`: the
call through the register follows somewhere in that function).  Prints addresses, names,
mnemonics and counts to the terminal; stores nothing.  Read only, single process.
"""

import argparse
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, ROOT  # noqa: E402

LIBS = ("wsock32.dll", "ws2_32.dll", "iphlpapi.dll")
MODULES = ["launcher.dll", "engine.dll", "client.dll", "server.dll"]
REFUSED = ("originsdk", "titanfall2.exe")


def ordinal_names():
    out = {}
    with open(os.path.join(ROOT, "runtime", "tools", "imports.def")) as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and not line.startswith("#") and p[2] != "-":
                out[(p[0].lower(), "#" + p[2])] = p[1]
    return out


def sites_of(m, names):
    """[(site, name, lib, via)] where via is 'call', 'load' or 'thunk@rva'."""
    slots = {s: (d, n) for s, (d, n) in m.import_slots().items() if d in LIBS}
    thunks = {}
    out = []
    for site, kind, slot in m.iat_refs(slots.keys()):
        d, n = slots[slot]
        n = names.get((d, n), n)
        if kind == "jmp" and m.func_of(site) is None:
            thunks[site] = (d, n)
        elif kind == "jmp":
            out.append((site, n, d, "tailjmp"))
        else:
            out.append((site, n, d, kind))
    for site, kind, tgt in m.rel_refs(thunks.keys()):
        d, n = thunks[tgt]
        out.append((site, n, d, "%s thunk@%x" % (kind, tgt)))
    out.sort()
    return out, slots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module", nargs="?")
    ap.add_argument("--fn", action="store_true")
    ap.add_argument("--args", type=int, default=0)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    names = ordinal_names()
    only = set(x for x in a.only.split(",") if x)
    mods = [a.module] if a.module else MODULES
    for name in mods:
        if any(r in name.lower() for r in REFUSED):
            raise SystemExit("refused: %s is never read" % name)
        m = Module(name)
        sites, slots = sites_of(m, names)
        if only:
            sites = [s for s in sites if s[1] in only]
        byfn = defaultdict(list)
        for site, n, d, via in sites:
            byfn[m.func_of(site)].append((site, n, d, via))
        print("%s: %d imports from the socket libraries, %d call sites in %d functions"
              % (name, len(slots), len(sites), len(byfn)))
        if not a.module:
            per = defaultdict(int)
            for _s, n, d, _v in sites:
                per[(d, n)] += 1
            for (d, n), c in sorted(per.items()):
                print("    %-12s %-18s %3d" % (d, n, c))
            continue
        if a.fn:
            strs = m.strings()
            fstr = defaultdict(list)
            for site, tgt in m.lea_refs():
                f = m.func_of(site)
                if f in byfn and tgt in strs:
                    fstr[f].append(strs[tgt][:60])
            exp = {r: n for n, _o, r in m.exports()}
            for f in sorted(byfn, key=lambda x: -1 if x is None else x):
                print("  f%s %s" % ("%x" % f if f is not None else "?", exp.get(f, "")))
                print("      calls: " + ", ".join("%s@%x" % (n, s) for s, n, _d, _v in byfn[f]))
                for s in fstr.get(f, [])[:12]:
                    print("      str %r" % s)
            continue
        md = None
        if a.args:
            import capstone
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        for site, n, d, via in sites:
            f = m.func_of(site)
            print("  %7x %-16s %-11s in f%s  %s" % (site, n, d, "%x" % f if f is not None else "?", via))
            if md is None:
                continue
            ent = m.entry_of(site)
            if not ent:
                continue
            ins = list(md.disasm(m.bytes_at(ent[0], ent[1] - ent[0]), ent[0]))
            i = next((k for k, x in enumerate(ins) if x.address == site), None)
            if i is None:
                print("        (site not on an instruction boundary of a linear decode)")
                continue
            for x in ins[max(0, i - a.args):i + 3]:
                print("        %7x %-7s %s" % (x.address, x.mnemonic, x.op_str))


if __name__ == "__main__":
    main()
