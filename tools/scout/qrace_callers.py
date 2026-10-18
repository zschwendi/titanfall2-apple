#!/usr/bin/env python3
"""Who reaches a function, by direct calls only (finding 36, the queue-race survey).

    .venv/bin/python tools/scout/qrace_callers.py MODULE.dll RVA [RVA ...] [--depth N] [--max N]
    .venv/bin/python tools/scout/qrace_callers.py MODULE.dll RVA [RVA ...] --roots [--depth N]

With --roots the tree is not printed: only the entry points met on the way up (exports,
table slots grouped by class, counts of lea references and of unreferenced functions).

Climbs the direct callers (call/jmp rel32 to a function start, raw byte scan as in
threads_lib.call_index) breadth first and prints, for every function met, how it can be
entered other than by a direct call: an export name, a slot of a table of code pointers in
data (with the class name when the table is a vtable with RTTI), or a `lea` of its address
in code. A function with none of these and no caller is printed as "no reference found".
An indirect call is never followed: a table slot or a lea means "callable by pointer, callers
unknown", not "not called".
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qrace_lib as Q  # noqa: E402


def vtable_name(m, slot):
    """(class name or None, slot index) of the code-pointer table that holds data address `slot`."""
    lo, hi = None, None
    for s in m.exec_sections():
        lo, hi = m.base + s["va"], m.base + s["va"] + s["vsize"]
        break
    a = slot
    while True:
        v = m.u64(a - 8)
        if v is None or not (lo <= v < hi):
            break
        a -= 8
    idx = (slot - a) // 8
    col = m.u64(a - 8)
    name = None
    if col and m.base < col < m.base + m.pe.size_of_image:
        raw = m.bytes_at(col - m.base, 20)
        if len(raw) == 20:
            sig, off, cd, td = struct.unpack_from("<IIII", raw)
            if sig == 1 and 0 < td < m.pe.size_of_image:
                n = m.pe.cstr(td + 0x10, 200)
                if n and n.startswith(".?A"):
                    name = n
    return name, idx, a


def describe(m, f, exp, cidx, lidx):
    notes = []
    if f in exp:
        notes.append("export %s" % exp[f][:60])
    for site in m.abs_refs([f]).get(f, [])[:4]:
        name, idx, start = vtable_name(m, site)
        notes.append("table 0x%x slot %d%s" % (start, idx, " of " + name if name else ""))
    for site in lidx.get(f, [])[:4]:
        g = m.func_of(site)
        notes.append("lea in f%s" % ("%x" % g if g is not None else "?"))
    return notes


def roots(m, t, exp, cidx, lidx, depth):
    seen, level, d = {t}, [t], 0
    direct = len({m.func_of(s) for s, k in cidx.get(t, [])} - {t, None})
    while level and d < depth:
        nxt = []
        for f in level:
            for s, k in cidx.get(f, []):
                c = m.func_of(s)
                if c is not None and c not in seen:
                    seen.add(c)
                    nxt.append(c)
        level, d = nxt, d + 1
    exports, classes, tables, leas, orphans, thunked = [], {}, 0, 0, 0, 0
    if level and not any(m.func_of(s) not in seen for f in level for s, k in cidx.get(f, [])):
        level = []
    for f in sorted(seen):
        if f in exp:
            exports.append(exp[f])
        refs = m.abs_refs([f]).get(f, [])
        for site in refs:
            name, idx, start = vtable_name(m, site)
            if name:
                classes.setdefault(name, set()).add(idx)
            else:
                tables += 1
        if lidx.get(f):
            leas += 1
        callers = {m.func_of(s) for s, k in cidx.get(f, [])} - {f, None}
        outside = any(m.func_of(s) is None for s, k in cidx.get(f, []))
        thunked += 1 if outside else 0
        if not callers and not outside and f not in exp and not refs and not lidx.get(f):
            orphans += 1
    print("  direct callers %d; functions met going up %d levels: %d%s" % (
        direct, d, len(seen), " (not exhausted)" if level else " (exhausted)"))
    print("  exports met: %d %s" % (len(exports), " ".join(e[:40] for e in exports[:8])))
    print("  functions with a lea of their address: %d; in tables without RTTI: %d; jumped to from code outside "
          "any unwind-table function: %d; with no reference at all: %d" % (leas, tables, thunked, orphans))
    for n in sorted(classes)[:25]:
        print("    vtable %s slots %s" % (n[:70], ",".join(str(i) for i in sorted(classes[n])[:8])))
    if len(classes) > 25:
        print("    ... %d classes in all" % len(classes))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    depth = int(sys.argv[sys.argv.index("--depth") + 1]) if "--depth" in sys.argv else 5
    cap = int(sys.argv[sys.argv.index("--max") + 1]) if "--max" in sys.argv else 40
    args = [a for a in args if a not in (str(depth), str(cap))] if len(args) > 2 else args
    name = args[0]
    if name.lower() in Q.EXCLUDED:
        raise SystemExit("out of scope")
    m = Q.Module(name)
    m.func_of = Q.root_functions(m.pe)[0]   # fragments belong to their whole function
    exp = {r: n for n, o, r in m.exports()}
    cidx = m.call_index()
    lidx = m.lea_index()
    for t in [int(x, 16) for x in args[1:]]:
        print("%s+0x%x" % (Q.short(name), t))
        if "--roots" in sys.argv:
            roots(m, t, exp, cidx, lidx, depth)
            continue
        seen, level, shown = {t}, [t], 0
        for d in range(depth + 1):
            nxt = []
            for f in level:
                callers = sorted({m.func_of(s) for s, k in cidx.get(f, []) if m.func_of(s) is not None} - {f})
                notes = describe(m, f, exp, cidx, lidx)
                if not callers and not notes:
                    notes = ["no reference found"]
                if shown < cap:
                    print("  %s f%-7x callers %-3d %s" % ("." * d, f, len(callers), "; ".join(notes)))
                shown += 1
                for c in callers:
                    if c not in seen:
                        seen.add(c)
                        nxt.append(c)
            level = nxt
            if not level:
                break
        if level:
            print("  (stopped at depth %d with %d more callers; %d functions met)" % (depth, len(level), len(seen)))
        elif shown > cap:
            print("  (%d functions met, %d shown)" % (shown, cap))


if __name__ == "__main__":
    main()
