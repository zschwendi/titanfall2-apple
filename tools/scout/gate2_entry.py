#!/usr/bin/env python3
"""What the entry point of tier0.dll / vstdlib.dll does, from static analysis (finding 21, section 5).

For each module: the entry stub, the C runtime start-up it reaches, the user `DllMain`, the
initializer tables `_cinit` walks, every static constructor in them, and the Windows imports each
stage reaches (in code order, first occurrence, with the call path).  Also lists lifter-skipped
functions on the start-up paths and the imports that could fail or block in a bare process.

    .venv/bin/python tools/scout/gate2_entry.py [--json out.json]

Facts here are tagged in the finding as verified (read from the disassembly and tables) or inferred
(a name given from the standard Visual Studio 2012 runtime shape).  Output: addresses, names, counts.
"""

import collections
import json
import sys

import gate2_lib as L

BARE_PROCESS_HAZARDS = {
    "file": {"CreateFileA", "CreateFileW", "CreateDirectoryA", "DeleteFileA", "DeleteFileW", "MoveFileA",
             "FindFirstFileA", "OpenFileMappingA", "CreateFileMappingA", "GetFileAttributesA", "WriteFile",
             "ReadFile", "FlushFileBuffers", "SetFilePointerEx"},
    "console": {"GetStdHandle", "GetConsoleMode", "GetConsoleCP", "WriteConsoleW", "ReadConsoleW", "AllocConsole",
                "SetConsoleTextAttribute", "GetConsoleScreenBufferInfo", "SetConsoleCtrlHandler", "SetStdHandle"},
    "thread": {"CreateThread", "ResumeThread", "SuspendThread", "OpenThread", "SetThreadAffinityMask",
               "SetThreadPriority", "SetProcessAffinityMask", "SetPriorityClass"},
    "wait or block": {"WaitForSingleObject", "WaitForMultipleObjects", "Sleep", "SleepConditionVariableSRW"},
    "window or message box": {"MessageBoxA", "CreateWindowExA", "FindWindowA", "EnumWindows", "GetSystemMetrics",
                              "RegisterClassExA", "SetWindowsHookExA", "FatalAppExitA"},
    "module loading": {"LoadLibraryA", "LoadLibraryW", "LoadLibraryExA", "LoadLibraryExW", "GetModuleHandleA",
                       "GetModuleHandleW", "GetProcAddress"},
    "process control": {"ExitProcess", "TerminateProcess", "CreateProcessA", "TerminateThread"},
    "timer resolution": {"timeBeginPeriod"},
    "debugger": {"IsDebuggerPresent", "OutputDebugStringA", "OutputDebugStringW", "UnhandledExceptionFilter",
                 "SetUnhandledExceptionFilter", "RaiseException"},
}


def init_tables(M, fn):
    """Initializer tables (lo, hi) found as adjacent `lea` pairs of non-code addresses in fn."""
    leas = []
    for addr, size, mn, tgt, fm in M.t["rip"]:
        if mn == "lea" and M.m.unit_of(addr) == fn and not M.m.in_exec(tgt) and M.pe.rva_to_off(tgt) is not None:
            leas.append((addr, tgt))
    leas.sort()
    out = []
    for (a1, t1), (a2, t2) in zip(leas, leas[1:]):
        lo, hi = min(t1, t2), max(t1, t2)
        if a2 - a1 <= 12 and (hi - lo) % 8 == 0 and 0 < hi - lo <= 0x8000:
            ents = [M.m.u64(r) for r in range(lo, hi, 8)]
            if all(v == 0 or M.m.in_exec(v - M.m.base) for v in ents):
                out.append((a1, lo, hi))
    return out


def first_imports(M, root, maxdepth=14):
    """Imports in the order a depth-first walk in code order first meets them: [(dll, name, path)]."""
    seen, order = set(), []
    sinks = M.sinks()

    def walk(fn, path):
        if fn in seen or len(path) > maxdepth:
            return
        seen.add(fn)
        for addr, kind, tgt in M.events(fn):
            if kind == "import":
                if tgt not in [(d, n) for d, n, _ in order]:
                    order.append((tgt[0], tgt[1], list(path)))
            elif tgt in sinks:
                continue
            else:
                walk(tgt, path + [tgt])
    walk(root, [root])
    return order


def describe(M, fn):
    ex = [e["name"] for e in M.by_rva.get(fn, [])]
    return ex[0] if ex else None


def analyze(name):
    M = L.load(name)
    R = {"module": name, "entry": M.pe.entry_rva}
    ev = M.events(M.pe.entry_rva, raw=True)
    calls = [t for a, k, t in ev if k == "call"]
    jmps = [t for a, k, t in ev if k == "jmp"]
    R["cookie_init"] = calls[0] if calls else None
    startup = jmps[0] if jmps else None
    R["startup"] = startup
    sev = M.events(startup, raw=True)       # raw targets: DllMain sits in the padding after another unit
    callee_seq = [t for a, k, t in sev if k == "call"]
    distinct = []
    for t in callee_seq:
        if t not in distinct:
            distinct.append(t)
    R["crt_init"] = distinct[0]
    R["dllmain"] = distinct[1]
    dm = M.disasm(R["dllmain"], 8)
    R["dllmain_insns"] = len(dm)
    R["dllmain_trivial"] = [m for _, m, _ in dm] == ["mov", "ret"] and dm[0][2].endswith(", 1")
    R["thread_attach_helpers"] = []
    # _CRT_INIT: callees in order; the one holding the initializer tables is _cinit
    cev = M.events(R["crt_init"])
    R["crt_init_calls"] = []
    cinit = None
    for a, k, t in cev:
        if k == "call":
            R["crt_init_calls"].append(t)
            if cinit is None and init_tables(M, t):
                cinit = t
    R["cinit"] = cinit
    tabs = init_tables(M, cinit)
    R["init_tables"] = []
    ctors = []
    for site, lo, hi in tabs:
        ents = []
        for r in range(lo, hi, 8):
            v = M.m.u64(r)
            ents.append((r, (v - M.m.base) if v else 0))
        R["init_tables"].append({"lo": lo, "hi": hi, "slots": len(ents), "nonnull": sum(1 for _, v in ents if v)})
        ctors.append((lo, hi, ents))

    # first Windows calls on the start-up path (cookie init, then _CRT_INIT)
    R["first_imports_cookie"] = [(d, n) for d, n, _ in first_imports(M, R["cookie_init"])]
    R["crt_steps"] = []
    for a, k, t in M.events(R["crt_init"]):
        if k == "import":
            R["crt_steps"].append({"site": a, "import": "%s!%s" % t})
        elif k == "call" and not any(x.get("rva") == t for x in R["crt_steps"]):
            c = M.closure(t)
            direct = sorted({n for ad, kk, n in M.events(t) if kk == "import" for n in [n[1]]})
            R["crt_steps"].append({"site": a, "rva": t, "insns": (M.rep(t) or {}).get("insns"), "status": M.status(t),
                                   "direct_imports": direct,
                                   "closure_imports": sorted({n for d, n in c["imports"]}),
                                   "closure_units": len(c["units"]), "skipped": ["%#x" % u for u in c["skipped"]]})
    fi = first_imports(M, R["crt_init"])
    R["first_imports_crt_init"] = [{"dll": d, "name": n, "path": ["%#x" % x for x in p]} for d, n, p in fi]

    # static constructors
    R["ctors"] = []
    entry_closure_units = set()
    entry_gw = set()
    entry_imports = collections.Counter()
    skipped = set()
    for lo, hi, ents in ctors:
        for r, v in ents:
            if not v:
                continue
            c = M.closure(v)
            direct = [t for a, k, t in M.events(v) if k in ("call", "jmp")]
            ex = [describe(M, t) for t in direct if describe(M, t)]
            imps = sorted(set(n for d, n in c["imports"]))
            R["ctors"].append({
                "table": "%#x" % lo, "slot_rva": r, "rva": v, "insns": (M.rep(v) or {}).get("insns"),
                "status": M.status(v), "closure_insns": c["insns"], "closure_units": len(c["units"]),
                "skipped_in_closure": ["%#x" % u for u in c["skipped"]],
                "indirect_calls": c["indirect"],
                "direct_callees": ["%#x" % t for t in direct],
                "export_callees": ex,
                "imports": imps,
                "globals_written": len(c["globals_written"]),
            })
            entry_closure_units |= c["units"]
            entry_gw |= c["globals_written"]
            entry_imports.update(c["imports"])
            skipped |= set(c["skipped"])
    # the C runtime start-up closure too
    for root in (R["cookie_init"], R["crt_init"], R["dllmain"]):
        c = M.closure(root)
        entry_closure_units |= c["units"]
        entry_gw |= c["globals_written"]
        entry_imports.update(c["imports"])
        skipped |= set(c["skipped"])
    R["entry_closure_units"] = len(entry_closure_units)
    R["entry_globals_written"] = sorted(entry_gw)
    R["entry_imports"] = sorted("%s!%s" % k for k in entry_imports)
    R["entry_skipped"] = []
    for u in sorted(skipped):
        r = M.rep(u)
        R["entry_skipped"].append({"rva": "%#x" % u, "insns": r["insns"], "reasons": sorted({x["what"] for x in r.get("reasons", [])}),
                                   "kind": sorted({x["kind"] for x in r.get("reasons", [])})})
    # hazards: which imports in the entry closure fall in a bare-process hazard class
    haz = collections.defaultdict(list)
    for (dll, n), cnt in entry_imports.items():
        for cls, names in BARE_PROCESS_HAZARDS.items():
            if n in names:
                haz[cls].append(n)
    R["hazards"] = {k: sorted(set(v)) for k, v in haz.items()}
    return M, R


def main():
    out = {}
    for name in L.MODULES:
        M, R = analyze(name)
        out[name] = R
        print("=== %s" % name)
        for k in ("entry", "cookie_init", "startup", "crt_init", "dllmain", "dllmain_insns", "cinit"):
            print("  %-14s %s" % (k, ("%#x" % R[k]) if isinstance(R[k], int) and k != "dllmain_insns" else R[k]))
        print("  init tables:", R["init_tables"])
        print("  first imports, cookie init:", R["first_imports_cookie"])
        print("  _CRT_INIT steps in code order:")
        for x in R["crt_steps"]:
            if "import" in x:
                print("    %#x import %s" % (x["site"], x["import"]))
            else:
                print("    %#x call %#x insns=%s %s direct=%s closure=%d imports, units=%d skipped=%s" % (
                    x["site"], x["rva"], x["insns"], x["status"], x["direct_imports"], len(x["closure_imports"]), x["closure_units"], x["skipped"]))
        print("  ctors: %d; entry closure units %d; entry imports %d" % (len(R["ctors"]), R["entry_closure_units"], len(R["entry_imports"])))
        for c in R["ctors"]:
            print("    ctor %#x insns=%s closure=%s skipped=%s imports=%d exports=%s callees=%s" % (
                c["rva"], c["insns"], c["closure_insns"], c["skipped_in_closure"], len(c["imports"]), c["export_callees"],
                c["direct_callees"][:4]))
        print("  skipped on start-up paths:", R["entry_skipped"])
        print("  hazards:", R["hazards"])
    if "--json" in sys.argv:
        p = sys.argv[sys.argv.index("--json") + 1]
        json.dump(out, open(p, "w"), indent=1, default=str)


if __name__ == "__main__":
    main()
