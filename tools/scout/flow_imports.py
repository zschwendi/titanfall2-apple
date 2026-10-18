#!/usr/bin/env python3
"""Imports a function reaches through direct calls, in code order (finding 37, question 7).

    .venv/bin/python tools/scout/flow_imports.py MODULE 0xFUNC [0xFUNC ...] [--depth N] [--win]
                                                 [--skip 0xFUNC,...] [--paths]

Walks the direct calls and tail jumps of each root depth-first, in the order the code has them,
down to N levels (default 6), and prints every import at its first occurrence with the depth
and the function that makes the call.  Calls through a register, a vtable or a global pointer
are not followed; their count is printed so the reader knows what the walk cannot see.
--win keeps only imports from outside the game's own DLLs and marks those that have no shim
today (runtime/shims, read the way tools/scout/boot_import_gap.py reads it).  --skip leaves out
subtrees (for example a fatal-error routine).  Prints addresses, names and counts.
OriginSDK.dll is refused.
"""

import os
import sys

from flow_lib import get, fmt_imp, ordinal_name
import boot_import_gap as gap

GAME_LIBS = {"tier0.dll", "vstdlib.dll", "launcher.dll", "engine.dll", "filesystem_stdio.dll",
             "datacache.dll", "inputsystem.dll", "localize.dll", "materialsystem_dx11.dll",
             "studiorender.dll", "vgui2.dll", "vguimatsurface.dll", "vphysics.dll",
             "rtech_game.dll", "client.dll", "server.dll", "bink2w64.dll", "binkawin64.dll",
             "mileswin64.dll", "originsdk.dll", "gfsdk_ssao.win64.dll", "amd_ags_x64.dll"}


def walk(M, roots, depth, skip):
    seen, order, blind = set(), [], [0, 0]

    def rec(f, d):
        if f in seen or f in skip:
            return
        seen.add(f)
        for ins in M.insns(f):
            if ins.mnemonic not in ("call", "jmp"):
                continue
            ct = M.call_target(ins)
            if not ct:
                continue
            k, v = ct
            if k == "import":
                order.append((v, d, f, ins.address))
            elif k == "direct":
                if ins.mnemonic == "jmp" and M.func_of(v) == M.func_of(ins.address) and M.func_of(v) is not None:
                    continue
                if d < depth:
                    rec(M.func_of(v) if M.func_of(v) is not None else v, d + 1)
                else:
                    blind[1] += 1
            elif ins.mnemonic == "call":
                blind[0] += 1
    for r in roots:
        rec(r, 0)
    return order, blind, len(seen)


def shim_state():
    shims = gap.load_shims()

    def state(lib, name):
        n = ordinal_name(lib, name)
        s = shims.get(gap.shim_dir(lib), {})
        st = s.get(n) or s.get(str(name))
        return "shim" if st and st != "trap" else "NO SHIM"
    return state


def main():
    a = sys.argv[1:]
    depth = int(a[a.index("--depth") + 1]) if "--depth" in a else 6
    skip = set(int(x, 16) for x in a[a.index("--skip") + 1].split(",")) if "--skip" in a else set()
    win = "--win" in a
    pos = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or a[i - 1] not in ("--depth", "--skip"))]
    M = get(pos[0])
    roots = [int(x, 16) for x in pos[1:]]
    order, blind, nf = walk(M, roots, depth, skip)
    state = shim_state() if win else None
    first = {}
    for imp, d, f, site in order:
        if win and imp[0] in GAME_LIBS:
            continue
        if imp not in first:
            first[imp] = (d, f, site, 1)
        else:
            d0, f0, s0, n = first[imp]
            first[imp] = (d0, f0, s0, n + 1)
    print("%s: %d function(s) walked from %s to depth %d; %d indirect call(s) not followed, %d direct "
          "call(s) below the depth limit" % (M.short, nf, ",".join("0x%x" % r for r in roots), depth, blind[0], blind[1]))
    missing = 0
    for imp, (d, f, site, n) in first.items():
        mark = ""
        if win:
            mark = state(imp[0], imp[1])
            missing += mark == "NO SHIM"
        print("  %-52s depth %d  first at %s+0x%x in %s  x%d  %s" % (fmt_imp(imp), d, M.short, site, M.label(f), n, mark))
    if win:
        print("%d import(s), %d without a shim" % (len(first), missing))


if __name__ == "__main__":
    main()
