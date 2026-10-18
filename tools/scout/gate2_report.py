#!/usr/bin/env python3
"""Writes docs/findings/21-gate2-exports.md and docs/findings/21-gate2-exports.json (finding 21).

Runs the four analyses (gate2_exports, gate2_entry, gate2_interfaces, gate2_candidates) and lays their
results out with the prose.  Facts that come out of the scripts are computed here; the prose around
them records what was read in the disassembly, each with its basis.  Nothing from the game binaries is
copied: addresses, names, counts and short identifiers only.

    .venv/bin/python tools/scout/gate2_report.py            # from the repository root
"""

import collections
import json
import os
import sys

import gate2_candidates
import gate2_entry
import gate2_exports
import gate2_interfaces
import gate2_lib as L

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_MD = os.path.join(REPO, "docs", "findings", "21-gate2-exports.md")
OUT_JSON = os.path.join(REPO, "docs", "findings", "21-gate2-exports.json")

# Names for functions the lifter skips, given from the instruction mix and call sites (inferred).
SKIP_LABELS = {
    ("tier0", 0x249c0): "memmove (C runtime; movnti)",
    ("tier0", 0x25bd0): "memset (C runtime; movnti)",
    ("tier0", 0x25f40): "strstr-like search (C runtime; SSE4.2 string compare)",
    ("tier0", 0x2ad48): "FLS/TLS wrapper: alloc (jmp rax)",
    ("tier0", 0x2ad64): "FLS/TLS wrapper: free (jmp rax)",
    ("tier0", 0x2ad80): "FLS/TLS wrapper: get value (jmp rax)",
    ("tier0", 0x2ad9c): "FLS/TLS wrapper: set value (jmp rax)",
    ("tier0", 0x2ae64): "InitializeCriticalSectionEx wrapper (jmp rax)",
    ("tier0", 0xf450): "clock-speed set-up (scalar double)",
    ("tier0", 0x10ba0): "logging helper (cvtps2pd)",
    ("tier0", 0x18290): "Plat_FloatTime (scalar double)",
    ("tier0", 0x184c0): "Plat_MSTime (scalar double)",
    ("tier0", 0x89a0): "JT_InitJobTypeForJobArrays (float)",
    ("tier0", 0xe940): "COM_TimestampedLog (float)",
    ("tier0", 0xec80): "CrackSmokingCompiler (float)",
    ("vstdlib", 0x2f5f0): "memmove (C runtime; movnti)",
    ("vstdlib", 0x30880): "memset (C runtime; movnti)",
    ("vstdlib", 0x2fad4): "sqrtf (sqrtss)",
    ("vstdlib", 0x28000): "float math (cvtsd2ss, cvttss2si)",
    ("vstdlib", 0x29d60): "float math (cvtdq2ps)",
    ("vstdlib", 0x361cc): "float math (cvtps2pd, cvtsd2ss)",
    ("vstdlib", 0x3c3d8): "FLS/TLS wrapper: alloc (jmp rax)",
    ("vstdlib", 0x3c3f4): "FLS/TLS wrapper: free (jmp rax)",
    ("vstdlib", 0x3c410): "FLS/TLS wrapper: get value (jmp rax)",
    ("vstdlib", 0x3c42c): "FLS/TLS wrapper: set value (jmp rax)",
    ("vstdlib", 0x552f0): "get MXCSR (stmxcsr)",
    ("vstdlib", 0x55300): "set MXCSR (ldmxcsr)",
    ("vstdlib", 0x5530a): "MXCSR helper (ldmxcsr, stmxcsr)",
    ("vstdlib", 0x21710): "CGaussianRandomStream::RandomFloat (float)",
    ("vstdlib", 0x218a0): "CUniformRandomStream::RandomFloat (float)",
    ("vstdlib", 0x21910): "CUniformRandomStream::RandomFloatExp (float)",
}


def report_dates():
    import time
    out = []
    for mn in L.MODULES:
        p = os.path.join(L.LIFTED, mn, mn + ".report.json")
        out.append("%s %s" % (mn, time.strftime("%Y-%m-%d %H:%M", time.localtime(os.path.getmtime(p)))) if os.path.exists(p) else mn + " absent")
    return ", ".join(out)


def esc(s):
    return str(s).replace("|", "\\|")


_MODS = {}


def skip_label(mod, rva_text):
    r = int(rva_text, 16) if isinstance(rva_text, str) else rva_text
    lab = SKIP_LABELS.get((mod, r))
    if lab:
        return lab
    if mod not in _MODS:
        _MODS[mod] = L.load(mod)
    rep = _MODS[mod].rep(r)
    if rep and rep.get("reasons"):
        return "not named; skipped for " + ", ".join(sorted({x["what"] for x in rep["reasons"]})[:3])
    return "not named"


def table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for r in rows:
        out.append("| " + " | ".join(esc(c) for c in r) + " |")
    return "\n".join(out)


def build_all():
    inv = gate2_exports.build()
    ent, entry_written = {}, {}
    for mn in L.MODULES:
        M, R = gate2_entry.analyze(mn)
        ent[mn] = R
        entry_written[mn] = set(R["entry_globals_written"])
    ifc = gate2_interfaces.build()
    cand = gate2_candidates.build(entry_written)
    return inv, ent, ifc, cand


def counts_section(inv):
    rows = []
    for mn in L.MODULES:
        i = inv[mn]
        rows.append([mn + ".dll", i["count"], i["functions"], i["data"], i["mangled"], i["plain_c"], i["forwarders"],
                     i["demangle_failures"], "%d lifted, %d without handlers, %d skipped" % (
                         i["lifter"].get("lifted", 0), i["lifter"].get("lifted-without-handlers", 0), i["lifter"].get("skipped", 0))])
    return table(["Module", "Exports", "Functions", "Data", "C++ (mangled)", "Plain C names", "Forwarders", "Demangle failures", "Lifter status of the functions"], rows)


def subsystem_summary(inv):
    out = []
    for mn in L.MODULES:
        c = collections.Counter(r["subsystem"] for r in inv[mn]["records"])
        f = collections.Counter(r["subsystem"] for r in inv[mn]["records"] if r["kind"] == "function")
        out.append("**%s.dll**\n" % mn)
        out.append(table(["Subsystem", "Exports", "Of which functions"], [[k, v, f.get(k, 0)] for k, v in sorted(c.items(), key=lambda kv: (-kv[1], kv[0]))]))
        out.append("")
    return "\n".join(out)


def entry_section(ent, ifc):
    t, v = ent["tier0"], ent["vstdlib"]
    out = []
    out.append("### 5.1 Both entry points are the stock Visual Studio 2012 C runtime start-up [V: disassembly of both]\n")
    rows = [
        ["Entry stub (`AddressOfEntryPoint`)", "%#x" % t["entry"], "%#x" % v["entry"],
         "On `DLL_PROCESS_ATTACH` calls the cookie initialiser, then jumps to the start-up routine with the three arguments unchanged."],
        ["Security-cookie initialiser", "%#x" % t["cookie_init"], "%#x" % v["cookie_init"],
         "Mixes the results of `GetSystemTimeAsFileTime`, `GetCurrentThreadId`, `GetCurrentProcessId` and `QueryPerformanceCounter` into the stack-protection cookie; any values work."],
        ["`__DllMainCRTStartup`", "%#x" % t["startup"], "%#x" % v["startup"],
         "Reason 1/2: optional raw-DllMain pointer (null in both), then `_CRT_INIT`, then the user `DllMain`; reason 0/3: `_CRT_INIT` last."],
        ["`_CRT_INIT`", "%#x" % t["crt_init"], "%#x" % v["crt_init"],
         "On attach: heap init, `_mtinit`, `_RTC_Initialize`, command line, environment strings, `_ioinit`, `_setargv`, `_setenvp`, `_cinit`."],
        ["User `DllMain`", "%#x" % t["dllmain"], "%#x" % v["dllmain"],
         "**2 instructions in both: returns TRUE** [V: the function is `mov eax,1; ret`]. There is no game code in `DllMain`; all initialisation is C runtime plus static constructors."],
        ["`_cinit`", "%#x" % t["cinit"], "%#x" % v["cinit"],
         "Calls the optional floating-point initialiser, then walks the two initialiser tables below, registers one `atexit` handler."],
        ["C initialiser table (`_initterm_e`)", "%d slots, %d used" % (t["init_tables"][0]["slots"], t["init_tables"][0]["nonnull"]),
         "%d slots, %d used" % (v["init_tables"][0]["slots"], v["init_tables"][0]["nonnull"]), "Each entry is C runtime code (heap, locale, stdio set-up); a non-zero return aborts the load."],
        ["C++ constructor table", "%d slots, %d used" % (t["init_tables"][1]["slots"], t["init_tables"][1]["nonnull"]),
         "%d slots, %d used" % (v["init_tables"][1]["slots"], v["init_tables"][1]["nonnull"]), "Static constructors, listed in 5.3 and 5.4."],
    ]
    out.append(table(["Item", "tier0.dll (RVA)", "vstdlib.dll (RVA)", "What it does"], rows))
    out.append("")
    out.append("The identification of `_CRT_INIT`, `_cinit` and the tables is **[I: the call order, the argument checks on the reason code "
               "(1 attach, 2 thread attach, 0 detach, 3 thread detach), and the two `lea` pairs bounding runs of code pointers match the Visual Studio 2012 "
               "static runtime exactly]**; the addresses and the contents of the tables are **[V: read from the PE]**. The binary was not matched against any "
               "runtime source.\n")
    out.append("**What differs between the two.** tier0's `_CRT_INIT` starts with `_heap_init`, which is just `GetProcessHeap`. vstdlib replaces it: its first step "
               "checks tier0's data export `g_pMemAllocSingleton`, calls tier0's `CreateGlobalMemAlloc` if it is empty, and stores the result [V: disassembly of vstdlib "
               "RVA 0x1e540]. vstdlib's `malloc`/`calloc` then allocate through slot 1 of that object [V: RVA 0x1e180]. So **tier0 must be fully loaded before vstdlib's entry "
               "point runs**, and vstdlib's C runtime heap is the game allocator, not `HeapAlloc`.\n")

    out.append("### 5.2 `_CRT_INIT` steps in code order, with the Windows functions each step reaches\n")
    out.append("**The first Windows calls each DLL makes, in order [V: disassembly of the cookie initialiser and of the first `_CRT_INIT` steps]:** "
               "`GetSystemTimeAsFileTime`, `GetCurrentThreadId`, `GetCurrentProcessId`, `QueryPerformanceCounter` (the cookie); then tier0 calls `GetProcessHeap` while vstdlib "
               "calls tier0's `CreateGlobalMemAlloc` instead; then `_mtinit` (`EncodePointer`, `GetModuleHandleW` and `GetProcAddress` for the newer kernel32 names, "
               "`InitializeCriticalSectionAndSpinCount` for the C runtime's locks, the FLS-or-TLS index allocation, one 0x478-byte allocation, `GetCurrentThreadId`); then "
               "`GetCommandLineA`, `GetEnvironmentStringsW`, `WideCharToMultiByte`, `FreeEnvironmentStringsW`, `GetStartupInfoW`, `GetFileType`, `GetStdHandle`, "
               "`GetModuleFileNameA`; then the initialiser tables. The order inside `_mtinit` is read from the step table below; it is not a trace.\n")
    for mn in L.MODULES:
        R = ent[mn]
        out.append("**%s.dll** (static reachability from each call, error and exit paths cut; \"skipped\" are the lifter's skipped functions inside it)\n" % mn)
        rows = []
        for x in R["crt_steps"]:
            if "import" in x:
                rows.append(["%#x" % x["site"], "direct import call", x["import"], "", ""])
                continue
            rows.append(["%#x" % x["site"], "%#x (%s insns)" % (x["rva"], x["insns"]), ", ".join(x["direct_imports"]) or "none directly",
                         "%d imports in %d functions" % (len(x["closure_imports"]), x["closure_units"]),
                         "; ".join("%s %s" % (u, skip_label(mn, u)) for u in x["skipped"]) or "none"])
        out.append(table(["Site", "Callee", "Direct Windows calls", "Closure", "Skipped by the lifter"], rows))
        out.append("")
    out.append("Reading of the first steps [V: disassembly of tier0 RVAs 0x31864, 0x2f79c]: `_heap_init` is one call to `GetProcessHeap`. `_mtinit` resolves the "
               "newer kernel32 names by `GetModuleHandleW` and `GetProcAddress` (the family finding 24 lists), allocates a per-thread-data index through the "
               "FLS-or-TLS wrapper, allocates a 0x478-byte per-thread block with the CRT allocator (`HeapAlloc`; `Sleep` appears only in the retry loop after a failed "
               "allocation [I: standard `_malloc_crt` shape]), stores the thread id in it. Environment strings, command line (`GetCommandLineA`), `GetModuleFileNameA`, "
               "`GetStartupInfoW`, `GetFileType`, `GetStdHandle` follow.\n")

    out.append("### 5.3 tier0.dll static constructors (%d)\n" % len(t["ctors"]))
    out.append(ctor_summary(ent["tier0"], "tier0"))
    out.append("### 5.4 vstdlib.dll static constructors (%d)\n" % len(v["ctors"]))
    out.append(ctor_summary(ent["vstdlib"], "vstdlib"))

    out.append("### 5.5 What a bare process would hit\n")
    out.append(
        "Findings that matter for a process with no console, no window, no registry and no other game DLLs. Each is tagged.\n\n"
        "1. **No thread, file, registry, window or message box is created or opened by either DLL's load path** [V: none of `CreateThread`, `CreateFile*`, `Reg*`, "
        "`CreateWindowEx*`, `MessageBox*` is in the pruned static closure of either entry point; the cut error and exit paths are listed in the JSON]. "
        "Static reachability is an upper bound: it follows branches that do not run.\n"
        "2. **tier0's load spins for about 31 ms on `QueryPerformanceCounter` reading `rdtsc`.** A static constructor (tier0 RVA 0x4f970, tail-jumping to 0xf450) calls "
        "`GetCPUInformation`; its first call computes `QueryPerformanceFrequency / 32`, reads `rdtsc`, loops until `QueryPerformanceCounter` has advanced by that amount, reads "
        "`rdtsc` again and stores the difference times 32 as the clock speed [V: disassembly of tier0 0xca90]. A counter that does not advance hangs the load; a TSC that is not "
        "cycle-rate gives a wrong `g_ClockSpeed`. Needs the lifter's `rdtsc` and `cpuid` (finding 16).\n"
        "3. **Topology probe at load.** `GetCPUInformation` asks for a CPU-topology object (RTTI classes `ICpuTopology` with `GlpiImpl`, `CpuidImpl`, `DefaultImpl`). The factory "
        "(tier0 0xd780) first runs a probe at 0xdf90 that calls `GetModuleHandleA(\"kernel32\")` and then `GetProcAddress` for `VerifyVersionInfoA`, `VerSetConditionMask` and "
        "`GetLogicalProcessorInformation`, and uses the `GetLogicalProcessorInformation` implementation if the OS check passes [V: strings and call order]. If `GetProcAddress` "
        "returns NULL for any of those, it falls to the CPUID implementation, which sets thread and process affinity masks core by core "
        "(`GetProcessAffinityMask`, `SetProcessAffinityMask`, `SetThreadAffinityMask`, `GetCurrentThread`, `Sleep`) [V for the calls, I for the selection logic]. "
        "**Recommendation: expose `VerifyVersionInfoA`, `VerSetConditionMask` and `GetLogicalProcessorInformation` (with the size-query protocol: first call with a NULL "
        "buffer returns FALSE and the needed length) on the native kernel32 module, so the load takes the path that touches no affinity state.**\n"
        "4. **Allocator built at load.** A static `CTSQueue` constructor (tier0 0x4fe40, via 0x1c3f0) allocates through the game allocator, so `CStdMemAlloc` is constructed "
        "in static storage during tier0's load; its constructor chain reaches `VirtualAlloc` [I: call path 0x4fe40 > 0x1c3f0 > `CreateGlobalMemAlloc` > 0x12a20 > 0x14480; not executed here]. "
        "Another constructor (0x4fb70) makes two `VirtualAlloc` reservations through 0x21500: a default-sized one and one of 0x1900000 bytes (25 MB) [V: the constant and the call]. "
        "That function also takes a lock whose contended path waits on a semaphore (`CreateSemaphoreA`, `WaitForSingleObject`); uncontended it does not wait [I].\n"
        "5. **`LoadLibraryA` and `GetProcAddress` at load.** tier0 RVA 0x4fc50 calls `Plat_GetProcAddress(\"Kernel32.dll\", \"TryEnterCriticalSection\")` and keeps the "
        "result at tier0 RVA 0x19a400, falling back to a blocking enter when it is NULL [V]. Native kernel32 must answer that lookup with a real stub for "
        "`CThreadMutex::TryLock` to be a try-lock.\n"
        "6. **`timeBeginPeriod` (winmm) is statically reachable** from the same constructor chain through `ThreadSleep`; it runs only if the allocator-publication spin loop sleeps [I]. "
        "It is the only import from outside kernel32 (and, for vstdlib, tier0) in either entry closure, and tier0 imports three functions from `OriginSDK.dll` that the entry path does not call [V: no "
        "`originsdk.dll` import in the entry closure].\n"
        "7. **vstdlib's load reaches 15 calls to a skipped `sqrtf`** (6-instruction constructors computing `sqrtf` of a constant at start-up; the callee at vstdlib "
        "0x2fad4 uses `sqrtss`) and two constructors (0x58eb0, 0x58f50) that call the skipped `memset` directly. They are not optional: the constructors run on every load [V: constructor table plus call targets].\n"
        "8. **Command line.** `GetCommandLineA` is read in `_CRT_INIT` of both DLLs; tier0's `CommandLine()` object is a separate, game-level object that stays empty until "
        "`CreateCmdLine` is called on it (the launcher or engine does that in the real game [I]). The C runtime parses `GetModuleFileNameA` and the command line for `argv`; neither is "
        "used by the engine code that this scout read [I].\n")

    out.append("### 5.6 What the lifter does not translate on the load path (the Gate 2 blockers)\n")
    out.append("Taken from the lifter's reports; every function below is reachable from an entry point by direct calls and is **skipped** today. A call to a skipped function "
               "ends in `rt_dispatch_miss` (RUNTIME.md section 2).\n")
    rows = []
    for mn in L.MODULES:
        for s in ent[mn]["entry_skipped"]:
            rows.append([mn, s["rva"], s["insns"], ", ".join(s["reasons"]), skip_label(mn, s["rva"]) or "not named"])
    out.append(table(["Module", "RVA", "Insns", "Reason (first unsupported)", "What it is (inferred)"], rows))
    out.append("")
    out.append("Summary by cause: **`movnti`** (memmove and memset in both DLLs, four functions), **`jmp rax` tail jumps** (the four FLS-or-TLS wrappers and one "
               "`InitializeCriticalSectionEx` wrapper in tier0; four wrappers in vstdlib), **scalar and packed floating point** (tier0's clock set-up; vstdlib's `sqrtf` and three "
               "float helpers), **SSE4.2 string compare** (tier0), and **`stmxcsr`/`ldmxcsr`** (vstdlib). The `jmp rax` wrappers are skipped whatever the runtime answers for "
               "`FlsAlloc`: the function as a whole contains the indirect jump. They sit in `_mtinit` and in every thread attach. Some entries are on branches that normally do not run: "
               "vstdlib 0x361cc and the three MXCSR helpers belong to the `sqrtf` error handler (reached for NaN, infinity or negative arguments) [I: the call sites are the error branches of 0x2fad4].\n")
    out.append("### 5.7 The Windows functions both loads need, and the names they look up\n")
    allimps = set()
    for mn in L.MODULES:
        allimps |= {x.split("!")[1] for x in ent[mn]["entry_imports"] if x.startswith(("kernel32", "winmm"))}
    allimps |= {"CreateEventA"}
    out.append("Union of the entry-point closures of both DLLs (error and exit paths cut; %d functions; `CreateEventA` added from tier0's `CThreadEvent` constructor, which vstdlib calls) "
               "[V: import call sites on the static paths of section 5.2 to 5.4]. This is a superset of what runs, and a lower bound for the shims wave 1 and 2 of finding 24 must "
               "answer before either DLL finishes loading, with the lifter gaps of 5.6 closed:\n\n%s\n" % (
                   len(allimps), ", ".join("`%s`" % n for n in sorted(allimps))))
    out.append("**Names looked up by string at load** (`GetProcAddress` on the native kernel32 module; a NULL answer is handled by each caller, but changes the path taken): the "
               "C runtime's 29 newer kernel32 names (finding 24, last section), `VerifyVersionInfoA`, `VerSetConditionMask`, `GetLogicalProcessorInformation` (topology factory, "
               "5.5 item 3) and `TryEnterCriticalSection` (5.5 item 5). tier0 also references `ntdll.dll` and `vtuneapi.dll` by name, but not on the load path [I: finding 24; the "
               "string users were not traced here].\n")
    return "\n".join(out)


CTOR_KINDS = [
    ("constant or flag store (1 to 3 instructions)", lambda c: (c["insns"] or 0) <= 3 and not c["direct_callees"]),
    ("registers an `atexit` handler after constructing a small object", lambda c: any(True for _ in c["direct_callees"]) and (c["insns"] or 0) <= 14),
]


def ctor_summary(R, mn):
    out = []
    ctors = R["ctors"]
    trivial = [c for c in ctors if (c["insns"] or 0) <= 3 and not c["direct_callees"]]
    rest = [c for c in ctors if c not in trivial]
    c_init = [c for c in ctors if c["table"] == "%#x" % R["init_tables"][0]["lo"]]
    out.append("%d entries in the C++ table plus %d C initialisers; %d are 1 to 3 instruction stores of a constant into a global and need nothing. The others:\n" % (
        sum(1 for c in ctors if c not in c_init), len(c_init), len(trivial)))
    cnt = collections.Counter(u for c in ctors for u in c["skipped_in_closure"])
    base = {u for u, n in cnt.items() if n >= max(3, len(ctors) // 4)}
    out.append("Almost every constructor registers an `atexit` handler, and the C runtime's `_onexit` path statically reaches the skipped functions %s "
               "(allocation, locking and per-thread data); they are left out of the last column below. Only the others are listed there.\n" %
               ", ".join("%s (%s)" % (u, skip_label(mn, u)) for u in sorted(base, key=lambda x: int(x, 16))))
    rows = []
    for c in rest:
        labels = []
        if c["export_callees"]:
            labels.append("calls " + ", ".join(sorted(set(x.split("@@")[0].lstrip("?") for x in c["export_callees"]))))
        if c in c_init:
            labels.append("C runtime initialiser")
        sk = [u for u in c["skipped_in_closure"] if u not in base]
        rows.append(["%#x" % c["rva"], c["insns"], c["closure_insns"], len(c["imports"]), ", ".join(labels),
                     "; ".join("%s %s" % (u, skip_label(mn, u)) for u in sk)])
    out.append(table(["Constructor", "Insns", "Static closure insns", "Windows imports reached", "Notes", "Skipped inside"], rows))
    out.append("")
    if mn == "tier0":
        out.append("Named from the strings and callees in their own bodies [V]: 0x4f9a0 registers the logging channel `General`, 0x4f940 the channel `LOADING`; "
                   "0x4fc90 names the main thread `MainThrd` and stores `GetCurrentThreadId`; 0x4fc50 looks up `TryEnterCriticalSection`; 0x4f900 and 0x4f920 construct the command-line "
                   "mutex and set the flag `CommandLine()` tests; 0x4fb70 reserves the two `VirtualAlloc` arenas; 0x4f970 runs `GetCPUInformation` and the clock set-up; the constructors "
                   "that call `Error` with a misalignment string build `CTSListBase` and `CTSQueue` objects (they call `InitializeSListHead`); three allocate thread-local indices (`TlsAlloc`). "
                   "Most of the remaining 6-instruction constructors build one static object and register its destructor.\n")
    else:
        out.append("Named from strings and callees [V]: 0x589a0, 0x58a70 and 0x58c80 register the three interfaces (section 6); 0x58c00 constructs the `CKeyValuesSystem` at vstdlib "
                   "RVA 0x78f90; 0x58bc0 constructs the default `CUniformRandomStream` (initialises its critical section); 15 constructors compute `sqrtf` of a constant; "
                   "the rest build static objects and register destructors. Four constructors call tier0 exports: 0x58910 `CThreadLocalBase` (`TlsAlloc`), 0x58940 `CThreadMutex` "
                   "(`InitializeCriticalSectionAndSpinCount`), 0x58af0 and 0x58b80 `CThreadEvent` (`CreateEventA`), so **vstdlib's load needs `CreateEventA` and `TlsAlloc` as well**; "
                   "all go through tier0's allocator.\n")
    return "\n".join(out)


def candidate_section(cand):
    out = []
    rows = []
    for c in cand["candidates"]:
        m = c["metrics"]
        sk = m["skipped_in_closure"]
        rows.append([c["rank"], c["title"].split(":")[0].split(" (")[0][:60], m["root_insns"], "yes" if m["roots_lifted"] else "NO",
                     "%d / %d" % (m["closure_insns"], m["closure_units"]),
                     (str(len(sk)) if sk else "0"), m["indirect_call_sites"], len(m["windows_imports"]) + len(m["tier0_imports"]),
                     m["globals_touched"], m["globals_set_by_startup_count"], ", ".join(m["uses"]) or "none"])
    out.append(table(["#", "Candidate", "Insns in the roots", "Roots lifted today", "Static closure: insns / functions",
                      "Skipped functions inside", "Indirect call sites", "Windows (and tier0) imports reachable", "Globals touched",
                      "Of which set by start-up code", "Static reach to threads / files / allocator"], rows))
    out.append("")
    out.append("How to read the columns. **Roots** are the exported functions (or vtable slots, given by RVA) the test calls. **Closure** follows direct calls and tail jumps "
               "from them using the lifter's function extents; it cuts the C runtime's exit, abort and console-message paths (units that call `ExitProcess`, `FatalAppExitA` "
               "or both `GetStdHandle` and `WriteFile`) and tier0's spew entry points (`Error`, `Warning`, `Msg`, ...), because every assert in the engine reaches them and no "
               "test takes them [V: computed by gate2_lib.py]. **Indirect call sites** are `call reg` / `call [mem]` the closure cannot follow (virtual calls, function pointers): "
               "the number is a floor on what else runs. **Roots lifted** is the lifter's status for the roots; **skipped inside** counts skipped functions anywhere in the "
               "closure, whether or not the executed path reaches them. **Set by start-up code** counts globals in the closure that the entry-point closure writes "
               "(constructor and C runtime stores): a non-zero value means the test is only meaningful after the DLL's entry point ran, and since C runtime globals are included "
               "it is high for anything that calls `malloc` or formats text. The curated lists of the shims on the executed path are in section 3; the full static lists are in the "
               "JSON.\n")
    return "\n".join(out)


def ranked_section(cand):
    out = []
    for c in cand["candidates"]:
        m = c["metrics"]
        out.append("#### %d. %s\n" % (c["rank"], c["title"]))
        out.append("- **Signature:** `%s`" % c["sig"])
        out.append("- **Basis for the signature:** %s" % c["basis"])
        out.append("- **Call and pass:** %s" % c["test"])
        out.append("- **Check:** %s" % c["check"])
        out.append("- **Expected answer comes from:** %s" % c["oracle"])
        out.append("- **Shims that must exist first (executed path):** %s" % (", ".join("`%s`" % s if not s.startswith("(") else s for s in c["shims"]) or "none"))
        out.append("- **Also needs:** %s" % c["requires"])
        sk = ["%s+%s %s" % (x.split("+")[0], x.split("+")[1], skip_label(x.split("+")[0], x.split("+")[1])) for x in m["skipped_in_closure"]]
        out.append("- **Measured:** %d instructions in the roots, lifted today: %s; static closure %d instructions in %d functions; skipped inside: %s; "
                   "indirect call sites: %d; Windows imports reachable (static): %s." % (
                       m["root_insns"], "yes" if m["roots_lifted"] else "**no**", m["closure_insns"], m["closure_units"],
                       "; ".join(sk) if sk else "none", m["indirect_call_sites"],
                       ", ".join(x.split("!")[1] for x in m["windows_imports"]) or "none"))
        out.append("")
    return "\n".join(out)


def bench_section(cand):
    out = []
    bs = sorted(cand["benchmarks"], key=lambda b: int(b["id"][1:]))
    rows = []
    for b in bs:
        m = b["metrics"]
        rows.append([b["id"], b["title"], b["kind"], "yes" if m["roots_lifted"] else "NO", m["closure_insns"], b["input"], b["notes"]])
    out.append(table(["Id", "Function", "Kind of code", "Lifted today", "Static closure insns", "Input that runs about 1 ms", "Notes"], rows))
    out.append("")
    out.append("The \"about 1 ms\" inputs are first guesses from the instruction counts above at an assumed 3 to 10 billion lifted instructions per second; "
               "time one call and scale the count or length once. Instruction counts per call are from the disassembly [V]; the per-call times are **estimates, not "
               "measurements** [I].\n")
    return "\n".join(out)


def interface_section(ifc, ent):
    out = []
    v = ifc["vstdlib"]
    out.append("### 6.1 Factories [V: export tables and disassembly]\n")
    out.append(table(["Module", "Factory exports", "Registered interfaces"], [
        ["tier0.dll", "none: no `CreateInterface`, and no interface list", "none"],
        ["vstdlib.dll", "`CreateInterface(const char* name, int* returnCode)` at RVA 0x26830; `VStdLib_GetICVarFactory()` at 0xf940 returns the address of a second copy of the same lookup (0x26140)",
         "%d, listed below" % len(v["registrations"])],
    ]))
    out.append("")
    out.append("`CreateInterface` reads the list head at vstdlib `.data` RVA 0x79c50. A node is `{create function, name pointer, next}` (24 bytes). It compares the argument with each "
               "node's name byte for byte (**case-sensitive**, stops at the terminator), then writes `*returnCode = 0` and tail-jumps to the node's create function, or writes 1 and "
               "returns NULL when nothing matches [V: disassembly of 0x26830]. The insertion routine (0x260a0) is a leaf with no unwind record; the lifter merges it into each of "
               "its three callers as a fragment, so it is not a separate function in the lifter's table.\n")
    rows = []
    for r in v["registrations"]:
        cls = r["class"][0][0] if r.get("class") else "unknown"
        rows.append([r["name"], "%#x" % r["ctor"], "%#x" % r["creator"], "%#x" % r["object"], cls])
    out.append(table(["Interface name", "Registering static constructor", "Create function", "Object it returns (RVA)", "Class (RTTI of the object's vptr)"], rows))
    out.append("")
    out.append("The three create functions are each `lea rax,[object]; ret`: the objects are statics in vstdlib's `.data` with the vptr already in the file image, so the "
               "interface pointer is valid as soon as the list node exists, but the object's own state (critical sections, tables) is filled by the static constructors, so "
               "calls into the object need the entry point to have run.\n")
    out.append("vstdlib's `.rdata` also holds about 25 other interface-version strings (`VFileSystem017`, `VMaterialSystem083`, ...). They are not registered here: "
               "they are names other modules ask for, kept in a table next to the demand-loader class `CDllDemandLoader` [I: location and neighbours]. tier0 holds the strings "
               "`Respawn001` and `Valve001`; their use was not determined.\n")
    out.append("### 6.2 Other objects the two DLLs hand out\n")
    out.append(table(["Object", "How to get it", "Class and vtable (RVA)", "Notes"], [
        ["Command line", "tier0 `CommandLine()` (0xc680)", "`CCommandLine`, 0x521a8, 16 slots", "Returns NULL until two static constructors ran; static object at tier0 RVA 0x6b040."],
        ["Game allocator", "tier0 `CreateGlobalMemAlloc()` (0x17b60) or data export `g_pMemAllocSingleton`", "`CStdMemAlloc`, 0x52fb8, 44 slots", "Built in static storage by the first caller; later callers spin with `ThreadSleep` until published."],
        ["Key-values symbol table", "vstdlib `KeyValuesSystem()` (0x1d7b0); `KeyValuesSystem_Resize` (0x1d7c0)", "`CKeyValuesSystem`, 0x5c0d8, 11 slots", "Static object at vstdlib RVA 0x78f90, built by a static constructor."],
        ["Default random stream", "vstdlib global pointer at RVA 0x745a8 (initialised to the static stream); replaced by `InstallUniformRandomStream`", "`CUniformRandomStream`, 0x5b020, 6 slots", "Static object at 0x745b0."],
        ["Thread pool", "vstdlib `CreateNewThreadPool()`, `DestroyThreadPool()`, data export `g_pThreadPool`", "`CThreadPool`, 0x5bcc8, 21 slots", "Creates worker threads unless told `-threads 1` or given a count of zero (finding 29)."],
        ["Logging listener and policy", "tier0 `LoggingSystem_RegisterLoggingListener`, `LoggingSystem_SetLoggingResponsePolicy`", "`ILoggingListener` 0x52b00 (2 slots), `CSimpleLoggingListener`, `CDefaultLoggingResponsePolicy`", "A test can register its own listener as a guest-ABI native object (RUNTIME.md 3.7)."],
    ]))
    out.append("")
    out.append("### 6.3 Methods simple enough to call from a test\n")
    out.append("\"Simple\" here is measured: the slot is lifted, nothing it can reach is skipped, its static closure is at most 400 instructions and it reaches no thread, file or process "
               "creation function. The table lists every slot of the classes worth testing with the numbers; the **names** are given only where a call site or the code shape "
               "agrees with the public Source SDK header order, and each carries its basis.\n")
    for c in ifc["classes"]:
        if c["class"] not in ("CCommandLine", "CStdMemAlloc", "CKeyValuesSystem", "CUniformRandomStream"):
            simple = [s["slot"] for s in c["slots"] if s["simple"]]
            out.append("**%s** (%s, vtable %#x): %d slots, %d meet the simple test; not decoded, so no names are given. Slots %s.\n" % (
                c["class"], c["module"], c["vtable_rva"], len(c["slots"]), len(simple), ", ".join(map(str, simple)) or "none"))
            continue
        out.append("**%s** (%s, vtable %#x, %d slots)\n" % (c["class"], c["module"], c["vtable_rva"], len(c["slots"])))
        rows = []
        for s in c["slots"]:
            rows.append([s["slot"], "%#x" % s["rva"], s["insns"], s["lifter"], s["closure_insns"], s["closure_skipped"], "yes" if s["simple"] else "no",
                         s["name"] or "not decoded", (s["basis"] or "")])
        out.append(table(["Slot", "RVA", "Insns", "Lifter", "Closure insns", "Skipped inside", "Simple", "Meaning", "Basis"], rows))
        out.append("")
    out.append("Of the interfaces registered through `CreateInterface`, `CCvar` (`VEngineCvar007`, 42 slots), `CEventSystem` (15) and `CProcessUtils` (11) have most slots simple by "
               "this measure, but nothing about their slot meanings was decoded here, and `CCvar` is a console-variable registry whose useful methods (find, register) "
               "pull in the allocator, list structures and the command line: not first-call material. `CProcessUtils` creates processes and pipes: do not call it.\n")
    return "\n".join(out)


def main():
    inv, ent, ifc, cand = build_all()
    md = []
    t, v = inv["tier0"], inv["vstdlib"]
    skipped_t = [r["text"] for r in t["records"] if r["kind"] == "function" and r["lifter"] == "skipped"]
    skipped_v = [r["text"] for r in v["records"] if r["kind"] == "function" and r["lifter"] == "skipped"]
    md.append("# Issue 21: what tier0.dll and vstdlib.dll export, and what to call first at Gate 2\n")
    md.append("Scouted 2026-10-08 from `game/bin/x64_retail/tier0.dll` and `vstdlib.dll` by static analysis only: nothing was executed, `Titanfall2.exe` was not read, no "
              "leaked source or symbol file was used. Generated by `tools/scout/gate2_report.py` (with `gate2_lib.py`, `gate2_demangle.py`, `gate2_exports.py`, `gate2_entry.py`, "
              "`gate2_interfaces.py`, `gate2_candidates.py`); the machine-readable form is `21-gate2-exports.json`. Addresses are RVAs; both DLLs have image base `0x180000000`. "
              "The lifter's reports were read from the main checkout's `lifted/tier0` and `lifted/vstdlib` (read-only; the files are dated %s); \"lifted\" below means that report's status. "
              "Other units may change the skip list: re-lift and rerun `gate2_report.py` to refresh every lifter column.\n" % report_dates())
    md.append("Tags: **[V: how]** verified from the PE tables or the disassembly, with the method; **[I: from what]** inferred from names, shapes or the public Source SDK headers "
              "(called out each time); **[?]** not determined. Public knowledge of the Source engine's documented interfaces (`tier0/icommandline.h`, `tier0/memalloc.h`, "
              "`vstdlib/strtools.h`, `tier1/interface.h`, `tier1/IKeyValuesSystem`) is used only for argument conventions and vtable order, and the text says where.\n")
    md.append("## Summary\n")
    lifted_t = t["lifter"].get("lifted", 0) + t["lifter"].get("lifted-without-handlers", 0)
    lifted_v = v["lifter"].get("lifted", 0) + v["lifter"].get("lifted-without-handlers", 0)
    first = cand["candidates"]
    md.append("1. tier0.dll exports %d symbols (%d functions, %d data), vstdlib.dll %d (%d, %d); no forwarders; all %d C++ names demangle, %d names are plain C and carry no argument types."
              % (t["count"], t["functions"], t["data"], v["count"], v["functions"], v["data"], t["mangled"] + v["mangled"], t["plain_c"] + v["plain_c"]))
    md.append("2. tier0 is locks and waitable objects (76; `CThreadMutex` is a thin `CRITICAL_SECTION` wrapper whose `Lock`/`Unlock` exports are the import thunks), the JT job system (40), "
              "the thread class (36), spew and asserts (34), call stacks (29), logging (25), timing (16); vstdlib is random numbers (25), coroutines (11), UTF string conversion (7), "
              "`CreateInterface`, `KeyValuesSystem`, thread-pool creation.")
    md.append("3. The lifter translates %d of tier0's and %d of vstdlib's exported functions; the 8 skipped are `Plat_FloatTime`, `Plat_MSTime`, `COM_TimestampedLog`, `CrackSmokingCompiler`, "
              "`JT_InitJobTypeForJobArrays` and three vstdlib random-float methods (scalar float/double SSE)." % (lifted_t, lifted_v))
    md.append("4. Both entry points are the stock VS2012 `_DllMainCRTStartup`; both `DllMain`s are `mov eax,1; ret`. All start-up work is C runtime set-up plus static constructors "
              "(tier0: %d C++ and %d C initialisers; vstdlib: %d and %d)." % (ent["tier0"]["init_tables"][1]["nonnull"], ent["tier0"]["init_tables"][0]["nonnull"], ent["vstdlib"]["init_tables"][1]["nonnull"], ent["vstdlib"]["init_tables"][0]["nonnull"]))
    md.append("5. **Neither DLL can finish loading on today's lifter output**: the C runtime's `memmove`/`memset` (`movnti`) and the FLS-or-TLS wrappers (`jmp rax`) are skipped in both and "
              "are called during start-up; vstdlib also calls a skipped `sqrtf` from 15 static constructors, tier0 a skipped clock set-up from one.")
    md.append("6. At load tier0 spins ~31 ms on `QueryPerformanceCounter` while reading `rdtsc`, probes `VerifyVersionInfoA`/`GetLogicalProcessorInformation` through `GetProcAddress`, looks up "
              "`TryEnterCriticalSection`, reserves two `VirtualAlloc` arenas (one 25 MB) and (by reachability) builds the game allocator; it creates no thread, file, window or registry key.")
    md.append("7. vstdlib's C runtime allocates from tier0's allocator (`g_pMemAllocSingleton`), so tier0 must load first. vstdlib registers three interfaces through `CreateInterface`: "
              "`VEngineCvar007` (CCvar), `EventSystem001`, `VProcessUtils002`; tier0 registers none.")
    md.append("8. Ranked first-call list: %d entries, cheapest first. Ranks 1 to 5 need no Windows function (`Plat_GetMemPageSize`, the mode flags, `EncodeBinaryToString`/`DecodeBinaryFromString`, "
              "the UCS-2 copies, `V_UTF8_strncpy`) and neither do 7 (thread-local read) and 8 (`CreateInterface`, which needs the entry point); 6 and 9 to 15 need one to seven kernel32 shims." % len(first))
    md.append("9. %d benchmark candidates covering tight integer loops, atomics, shim crossing, virtual dispatch, allocation, string compare and conversion; none has a measured time (static analysis only)." % len(cand["benchmarks"]))
    md.append("10. Not determined: slot meanings of `CCvar`, `CEventSystem`, `CProcessUtils` and slots 7 to 10 of `CKeyValuesSystem`; argument types of most plain-C exports beyond the ranked list; "
              "which load-time branches actually execute (only reachability is known).")
    md.append("")

    md.append("## 1. Export inventory\n")
    md.append(counts_section(inv))
    md.append("")
    md.append("Function or data is decided by the section the export RVA falls in: executable section means function [V: PE section flags]. The 16 data exports of tier0 are the vtable "
              "symbols (`CThread`, `BasicStatStructFieldDesc`, `CallStackStatStructDescFuncs`), the clock-speed values, the mini-profiler pointers, `g_pMemAllocSingleton`, "
              "`g_pakLoadApi`, `job_JT_ExecuteCall` and `g_nThreadID` (a `CThreadLocalInt<int>`); vstdlib's are the `CUniformRandomStream` vtable, `g_VCoverage` and `g_pThreadPool`.\n")
    md.append("**Demangler.** `gate2_demangle.py` is a small recursive-descent decoder for the Microsoft x64 scheme: member functions with access and virtual/static letters, "
              "constructors, destructors, operators, default-constructor closures, vftables, data, templates with type and integer arguments, pointers (including `__ptr64`), "
              "references, function pointers, enums, classes, structs, arrays, builtin types, back-references to names and to function parameters. All %d C++ names decode; "
              "it has a self-test (`--selftest`) of nine hand-checked cases. **Not covered** (none occurs here): pointers to members, non-type template arguments other than "
              "integers, `$$` pack and alias forms, MD5-hashed names. The back-reference rule for parameters of function-pointer types (the pointee's own parameters are "
              "numbered before the pointer itself) was fixed by checking that `JTGuts_RegisterJobType`'s later references resolve to a sensible job signature [I].\n" % (t["mangled"] + v["mangled"]))
    md.append("Arguments of the 205 plain-C exports are **unknown from the name**; where this finding gives them (the ranked list), they were read from the disassembly and say so.\n")
    md.append("### 1.1 By subsystem\n")
    md.append("The grouping is the scout's, made from names alone (`gate2_lib.subsystem`).\n")
    md.append(subsystem_summary(inv))
    md.append("### 1.2 tier0.dll, every export\n")
    md.append("Lifter column: L lifted, W lifted without exception handlers, S skipped (first reasons in brackets). \"Types from\" says whether argument types came from the mangled name.\n")
    md.append(gate2_exports.md_tables(inv, "tier0"))
    md.append("### 1.3 vstdlib.dll, every export\n")
    md.append(gate2_exports.md_tables(inv, "vstdlib"))

    md.append("## 2. Measured properties of the candidate functions\n")
    md.append("For each of the %d ranked candidates (several functions each, see section 3) the table gives what the binary says. The sections 3 and 4 give signatures, tests and shims." % len(first))
    md.append("")
    md.append(candidate_section(cand))

    md.append("## 3. The first-call list, cheapest first\n")
    md.append("Ordering rule: first whether the roots and everything they call are lifted, then how many Windows functions the executed path needs, then whether the entry points must have run, "
              "then whether threads, files or the allocator are involved. Item 16 (`Plat_gmtime`) and items 24 to 29 are placed by cost although the lifter blocks all or part of them today; "
              "the blocker is stated in each.\n")
    md.append("**Common prerequisites for every item:** the loader maps tier0 and vstdlib at their bases with relocations applied and the TLS template installed (RUNTIME.md 1.2 to 1.6), "
              "the dispatch table covers both modules, and `rt_module_export` resolves names (the mangled names above are the keys). Items that say \"needs the entry point\" additionally need "
              "the load of section 5 to have completed, which today is blocked by the lifter gaps in 5.6. To test something *before* those are fixed, use items 1 to 7 only with the "
              "entry point skipped: they touch only their own bytes of `.data`/`.tls` and have no static-constructor dependency.\n")
    md.append(ranked_section(cand))

    md.append("## 4. Benchmark candidates\n")
    md.append(bench_section(cand))

    md.append("## 5. What the two entry points do\n")
    md.append(entry_section(ent, ifc))

    md.append("## 6. Interfaces\n")
    md.append(interface_section(ifc, ent))

    md.append("## 7. Could not determine\n")
    md.append(
        "- **Which load-time branches run.** Everything in section 5 is reachability. Whether the topology probe takes the `GetLogicalProcessorInformation` path, "
        "whether the allocator's construction contends, whether any `Sleep` or semaphore wait executes, depends on the answers the runtime gives; the first run under the "
        "runtime (or a Proton trace, RUNTIME.md E1) settles it.\n"
        "- **Slot meanings** of `CCvar` (42), `CEventSystem` (15), `CProcessUtils` (11), `IThreadPool` (21), and slots 7 to 10 of `CKeyValuesSystem` and slots 2, 5, 6, 13 and 14 of `CCommandLine`. "
        "The names given elsewhere rest on call sites or on code shape plus the public header order, each tagged.\n"
        "- **Argument types of the plain-C exports** outside the ranked list, including `GetCurrentDate`'s output arguments, `Plat_GetTimeString`, all `LoggingSystem_*` except the two "
        "read, the `Print_*` functions, `Plat_GetPagedPoolInfo`, `InitPME`/`ShutdownPME` and the `Error*`/`Warning*` family beyond their format-string-first shape.\n"
        "- **Return type of `AllocateThreadID`.** The code leaves `eax` as the last call returned it, so it is treated as `void`; a caller that reads the result would read garbage [I].\n"
        "- **Whether `CStdMemAlloc` slot 8 is `GetSize`** (inferred from position and shape) and whether the `Alloc(size, file, line)` twins behave as the plain ones.\n"
        "- **What `Respawn001` and `Valve001` in tier0 are for**, what vstdlib's `VStdLib_GetICVarFactory` is called with, and who calls the 25 interface-version strings in vstdlib's `.rdata`.\n"
        "- **Per-call times.** The benchmark inputs are sized from instruction counts and an assumed rate, not from any run.\n"
        "- **The x86 oracle on whole closures.** The harness's Unicorn oracle (`harness/difftest.py`, class `Oracle`) runs one function with calls out stubbed; it can give expected answers for the leaf "
        "candidates (ranks 1 to 7, B1, B2) but there is no full-image x86 process to run the C runtime and the static constructors under. Where a test needs that, the expected values come from a "
        "model or from the host library, as stated per item.\n")
    md.append("## 8. Method\n")
    md.append(
        "- Exports: the PE export directory (`tools/inventory/pe_inventory.py` plus `gate2_lib.read_exports`, which also keeps ordinals and RVAs).\n"
        "- Disassembly: Capstone 5 through `tools/scout/xref_lib.py` (cached cross-references of every instruction with a RIP-relative operand, every direct call and jump, every "
        "indirect call); `gate2_disasm.py` prints annotated listings for reading.\n"
        "- Function extents and lifter status: the lifter's `report.json` (function ranges, instruction counts, skip reasons). The lifter merges unwind-less tail-jump targets into "
        "their callers as fragments; `gate2_lib.Mod.covering` maps an address to the function whose ranges contain it.\n"
        "- Closures: direct calls and tail jumps only; imports are found at `call [slot]` and `jmp [slot]` sites and through `jmp [slot]` thunks; virtual and pointer calls are "
        "counted but not followed.\n"
        "- Not used: any source code, symbol file, or runtime trace of the game; nothing was run.\n")
    os.makedirs(os.path.dirname(OUT_MD), exist_ok=True)
    with open(OUT_MD, "w") as f:
        f.write("\n".join(md) + "\n")

    j = {"inventory": {mn: {k: v_ for k, v_ in inv[mn].items() if k != "records"} for mn in L.MODULES},
         "exports": {mn: inv[mn]["records"] for mn in L.MODULES},
         "entry": {mn: {k: v_ for k, v_ in ent[mn].items()} for mn in L.MODULES},
         "interfaces": ifc, "candidates": cand["candidates"], "benchmarks": cand["benchmarks"]}
    with open(OUT_JSON, "w") as f:
        json.dump(j, f, indent=1, default=lambda o: sorted(o) if isinstance(o, (set, frozenset)) else str(o))
    print("wrote", OUT_MD, os.path.getsize(OUT_MD), "bytes;", OUT_JSON, os.path.getsize(OUT_JSON), "bytes")


if __name__ == "__main__":
    main()
