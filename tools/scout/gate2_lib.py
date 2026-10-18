#!/usr/bin/env python3
"""Shared analysis layer for the Gate 2 exports scout (finding 21).

Loads tier0.dll or vstdlib.dll through the existing scout context (xref_lib, scout_ctx), adds the
export table with ordinals, the lifter's report (read-only, from the main checkout's lifted/
directory), a per-function direct-call closure, the Windows imports each function reaches, the
globals it touches, and a few disassembly helpers.

Prints and returns addresses, names, counts and small mnemonic summaries only; never bytes of the
game.  Results that other gate2_*.py scripts need are cached as JSON in the scratch directory.
"""

import bisect
import collections
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
os.environ.setdefault("SCOUT_CACHE", os.path.join(os.environ.get("TMPDIR", "/tmp"), "tf2-gate2-cache"))
LIFTED = os.environ.get("TF2_LIFTED_DIR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lifted"))

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

import scout_ctx  # noqa: E402
import xref_lib  # noqa: E402
from gate2_demangle import demangle  # noqa: E402

MODULES = ("tier0", "vstdlib")
DATA_SECTIONS = (".data", ".rdata", "_RDATA", ".tls")


def read_exports(pe):
    """Every export: dicts with ordinal, name, rva, section, kind ('function'|'data'), forwarder."""
    rva, size = pe.dirs[0]
    off = pe.rva_to_off(rva)
    (_, _, _, _, _, base, nfunc, nnames, addr_rva, names_rva, ord_rva) = struct.unpack_from("<IIHHIIIIIII", pe.data, off)
    aoff, noff, ooff = pe.rva_to_off(addr_rva), pe.rva_to_off(names_rva), pe.rva_to_off(ord_rva)
    names = {}
    for i in range(nnames):
        (nrva,) = struct.unpack_from("<I", pe.data, noff + 4 * i)
        (idx,) = struct.unpack_from("<H", pe.data, ooff + 2 * i)
        names[idx] = pe.cstr(nrva)
    out = []
    for i in range(nfunc):
        (frva,) = struct.unpack_from("<I", pe.data, aoff + 4 * i)
        if frva == 0:
            continue
        sec = pe.section_of(frva)
        sname = sec["name"] if sec else "?"
        out.append({
            "ordinal": base + i, "name": names.get(i), "rva": frva, "section": sname,
            "kind": "function" if sec is not None and pe.is_exec(sec) else "data",
            "forwarder": rva <= frva < rva + size,
        })
    return out


class Mod:
    """One of the two DLLs with everything the scout scripts ask about it."""

    def __init__(self, name):
        self.name = name
        self.ctx = scout_ctx.Ctx(name)
        self.m = self.ctx.mod
        self.pe = self.m.pe
        self.t = self.ctx.t
        self.exports = read_exports(self.pe)
        for e in self.exports:
            e["dem"] = demangle(e["name"]) if e["name"] else None
        self.by_rva = collections.defaultdict(list)
        for e in self.exports:
            self.by_rva[e["rva"]].append(e)
        rp = os.path.join(LIFTED, name, name + ".report.json")
        self.report = json.load(open(rp)) if os.path.exists(rp) else None
        self.rfun = {}
        self.rbegins = []
        if self.report:
            for f in self.report["functions"]:
                f["rva_i"] = int(f["rva"], 16)
                self.rfun[f["rva_i"]] = f
            self.rbegins = sorted(self.rfun)
        self.slots = self.m.iat_slots()
        self._prep()

    # ---- indexes -------------------------------------------------------------------------
    def owner(self, addr):
        """Function (start RVA) an instruction address belongs to: the lifter's function ranges first (they
        split code that has no unwind record correctly), then the unwind/int3 based unit."""
        o = self.covering(addr) if self.report else None
        return o if o is not None else self.m.unit_of(addr)

    def _prep(self):
        m, t = self.m, self.t
        self.thunks = self.ctx.import_thunks()                       # thunk_rva -> (dll, name)
        self.import_refs = collections.defaultdict(list)             # unit -> [(dll,name)] referenced directly
        for addr, size, mn, tgt, fm in t["rip"]:
            s = self.slots.get(tgt)
            if s is not None and mn in ("call", "jmp"):
                u = self.owner(addr)
                if u is not None and addr not in self.thunks:
                    self.import_refs[u].append((s[0], s[1]))
        self.import_data = collections.defaultdict(set)              # unit -> {(dll, name)} slots read as data
        for addr, size, mn, tgt, fm in t["rip"]:
            s_ = self.slots.get(tgt)
            if s_ is not None and mn not in ("call", "jmp"):
                u = self.owner(addr)
                if u is not None:
                    self.import_data[u].add((s_[0], s_[1]))
        self.ind_calls = collections.Counter()
        for addr, mn, ops in t["ind"]:
            u = self.owner(addr)
            if u is not None:
                self.ind_calls[u] += 1
        self.data_refs = collections.defaultdict(lambda: {"r": set(), "w": set()})
        for addr, size, mn, tgt, fm in t["rip"]:
            sec = m.section_name(tgt)
            if sec in (".data", ".tls") and tgt not in self.slots:
                u = self.owner(addr)
                if u is None:
                    continue
                write = fm and mn in ("mov", "add", "sub", "or", "and", "xor", "inc", "dec", "xchg", "lock",
                                      "movups", "movaps", "movdqa", "movdqu", "movss", "movsd", "movq", "movd",
                                      "cmpxchg", "xadd", "bts", "btr", "neg", "not", "shl", "shr", "sar", "stosq")
                (self.data_refs[u]["w"] if write else self.data_refs[u]["r"]).add(tgt)

    def events(self, fn, raw=False):
        """Calls and tail jumps made by unit fn in address order: ('call'|'jmp', target_rva) for code
        or ('import', (dll, name)).  Uses the lifter report's ranges for the extent of fn."""
        if not hasattr(self, "_sites"):
            sites = []
            for addr, mn, tgt in self.t["calls"]:
                if mn in ("call", "jmp"):
                    sites.append((addr, mn, tgt, None))
            for addr, size, mn, tgt, fm in self.t["rip"]:
                s_ = self.slots.get(tgt)
                if s_ is not None and mn in ("call", "jmp") and addr not in self.thunks:
                    sites.append((addr, mn, None, (s_[0], s_[1])))
            sites.sort(key=lambda x: x[0])
            self._sites = sites
            self._site_addrs = [x[0] for x in sites]
        r = self.rep(fn)
        ranges = r["ranges"] if r else [[fn, fn + 0x40]]
        out = []
        for lo, hi in sorted(ranges):
            i = bisect.bisect_left(self._site_addrs, lo)
            while i < len(self._sites) and self._sites[i][0] < hi:
                addr, mn, tgt, imp = self._sites[i]
                i += 1
                if imp is not None:
                    out.append((addr, "import", imp))
                    continue
                if tgt in self.thunks:
                    out.append((addr, "import", self.thunks[tgt]))
                    continue
                if mn == "jmp" and any(lo2 <= tgt < hi2 for lo2, hi2 in ranges):
                    continue
                if raw:
                    out.append((addr, mn, tgt))
                    continue
                u = self.m.unit_of(tgt)
                if u is not None:
                    out.append((addr, mn, u))
        return out

    def callees(self, fn):
        """Direct callees of a function with import thunks resolved (kind 'import') and the rest as the
        start of the callee's function ('code').  Uses the lifter's function extents when fn is in its
        report, so code that merely follows a function in memory is not attributed to it."""
        out = []
        if self.rep(fn):
            for addr, kind, tgt in self.events(fn, raw=True):
                if kind == "import":
                    out.append(("import", tgt))
                elif tgt in self.thunks:
                    out.append(("import", self.thunks[tgt]))
                else:
                    u = tgt if tgt in self.rfun else self.owner(tgt)
                    if u is not None:
                        out.append(("code", u))
            return out
        for c in self.ctx.g.callees.get(fn, ()):
            if c in self.thunks:
                out.append(("import", self.thunks[c]))
            else:
                u = self.owner(c)
                if u is not None:
                    out.append(("code", u))
        return out

    # ---- lifter status -------------------------------------------------------------------
    def rep(self, fn):
        """Report record of the lifted function that starts at fn (or None)."""
        return self.rfun.get(fn)

    def covering(self, rva):
        """Report function whose ranges contain rva (the lifter merges unwind-less tail-jump targets into
        their callers as fragments), or None."""
        if not hasattr(self, "_cov"):
            iv = []
            for f in (self.report or {}).get("functions", []):
                for lo, hi in f["ranges"]:
                    iv.append((lo, hi, f["rva_i"]))
            iv.sort()
            self._cov = iv
            self._cov_lo = [x[0] for x in iv]
        i = bisect.bisect_right(self._cov_lo, rva) - 1
        for _ in range(64):
            if i < 0:
                break
            lo, hi, owner = self._cov[i]
            if lo <= rva < hi:
                return owner
            i -= 1
        return None

    def status(self, fn):
        r = self.rep(fn)
        if r:
            return r["status"]
        o = self.covering(fn)
        if o is not None:
            return self.rfun[o]["status"]
        return "not-in-report"

    # ---- closure -------------------------------------------------------------------------
    def sinks(self):
        """CRT exit, abort and console-message units: a closure stops at them (they are error paths
        that every C-runtime call can reach statically and no test would take)."""
        if getattr(self, "_sinks", None) is not None:
            return self._sinks
        want_exit = {"ExitProcess", "FatalAppExitA", "TerminateProcess"}
        direct = collections.defaultdict(set)
        for u, l in self.import_refs.items():
            for d, n in l:
                direct[u].add(n)
        for ta, (d, n) in self.thunks.items():
            for site, mn_, cf in self.ctx.callers(ta):
                if cf is not None:
                    direct[cf].add(n)
        sinks = set()
        for u, names in direct.items():
            if names & want_exit and not self.by_rva.get(u):
                sinks.add(u)
            if {"GetStdHandle", "WriteFile"} <= names and not self.by_rva.get(u):
                sinks.add(u)
        self._sinks = sinks
        return sinks

    SPEW_EXPORTS = {"Error", "Warning", "Msg", "DevMsg", "DevWarning", "ConMsg", "ConDMsg", "ConColorMsg", "CoreMsgV",
                    "LoggingSystem_Log", "LoggingSystem_LogV", "LoggingSystem_LogDirect", "Plat_SimpleLog",
                    "Error_SpewCallStack", "Warning_SpewCallStack", "COM_TimestampedLog", "DoNewAssertDialog",
                    "_ExitOnFatalAssert"}

    def spew_units(self):
        if not hasattr(self, "_spew"):
            self._spew = set()
            for e in self.exports:
                d = e["dem"] or {}
                nm = d.get("member") if d.get("scope") is None else None
                if e["kind"] == "function" and (e["name"] in self.SPEW_EXPORTS or nm in self.SPEW_EXPORTS):
                    self._spew.add(e["rva"])
        return self._spew

    def closure(self, root, limit=20000, prune=True, cut_spew=True):
        """Everything reachable from root by direct calls and tail jumps.  Returns a dict with the
        set of code units, the imports (dll, name), the set of skipped units, the indirect-call
        count, the globals read/written and the instruction total."""
        seen = {root}
        work = [root]
        imports = collections.Counter()
        if root in self.thunks:
            imports[self.thunks[root]] += 1
        sinks = self.sinks() if prune else set()
        if cut_spew and self.name == "tier0":
            sinks = sinks | self.spew_units()
        cut = set()
        data_imports = set()
        while work and len(seen) < limit:
            f = work.pop()
            for imp in self.import_refs.get(f, ()):
                imports[imp] += 1
            data_imports |= self.import_data.get(f, set())
            for kind, v in self.callees(f):
                if kind == "import":
                    imports[v] += 1
                elif v in sinks and v != root:
                    cut.add(v)
                elif v not in seen:
                    seen.add(v)
                    work.append(v)
        skipped = sorted(u for u in seen if self.status(u) == "skipped")
        absent = sorted(u for u in seen if self.rep(u) is None)
        gr, gw = set(), set()
        for u in seen:
            gr |= self.data_refs[u]["r"] if u in self.data_refs else set()
            gw |= self.data_refs[u]["w"] if u in self.data_refs else set()
        return {
            "units": seen, "cut": cut, "data_imports": data_imports, "imports": imports, "skipped": skipped, "not_in_report": absent,
            "indirect": sum(self.ind_calls.get(u, 0) for u in seen),
            "insns": sum((self.rep(u) or {}).get("insns", 0) for u in seen),
            "globals_read": gr, "globals_written": gw,
        }

    # ---- disassembly (for local inspection only; nothing is stored) ----------------------
    def disasm(self, rva, n=40, stop_at_ret=True):
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = False
        off = self.pe.rva_to_off(rva)
        data = self.pe.data[off:off + 16 * n]
        out = []
        for ins in md.disasm(data, rva):
            out.append((ins.address, ins.mnemonic, ins.op_str))
            if len(out) >= n:
                break
            if stop_at_ret and ins.mnemonic in ("ret", "int3"):
                break
        return out

    def fsize_bytes(self, fn):
        return self.ctx.fsize(fn)


def load(name):
    return Mod(name)


def subsystem(name, dem=None):
    """Group an export by subsystem from its name alone (the grouping is the scout's, not the engine's)."""
    n = name or ""
    d = dem or {}
    scope = d.get("scope") or ""
    member = d.get("member") or n
    base = scope.split("::")[0] if scope else ""
    if not scope and n.startswith("?") and not n.startswith("??"):
        n = member                      # a free C++ function: judge it by its bare name
    if n.startswith("Coroutine_"):
        return "coroutines"
    if n == "g_nThreadID":
        return "thread class and thread-local storage"
    if n.startswith("??_7"):
        return "vtables (data)"
    if base in ("CThreadMutex", "CThreadFullMutex", "CThreadRWLock", "CThreadSpinRWLock", "CThreadFastMutexSlow"):
        return "locks"
    if base in ("CThreadEvent", "CThreadSemaphore", "CThreadSyncObject"):
        return "waitable objects (events, semaphores)"
    if base in ("CThread",) or base.startswith("GenericThreadLocals") or "ThreadLocal" in n:
        return "thread class and thread-local storage"
    if "CStackTop" in n or "CCallStack" in n or "CallStackStat" in n or "BasicStatStruct" in n:
        return "call-stack capture and stats"
    if base.startswith("CAlignedNewDelete"):
        return "memory allocator"
    if n.startswith("?JT") or "JTGuts" in n or n.startswith("JT_") or "job_JT" in n or n == "ConfigureAndInitJobThreads":
        return "job threads (JT)"
    if n.startswith(("Thread", "?Thread")) or n in ("AllocateThreadID", "FreeThreadID", "CreateSimpleThread",
                                                     "DeclareCurrentThreadIsMainThread", "DeclareCurrentThreadIsServerFrameThread",
                                                     "DeclareCurrentThreadNotServerFrameThread", "ReleaseThreadHandle",
                                                     "SetLoadingThreadDuringVideo", "RunTSListTests", "RunTSQueueTests",
                                                     "RunThreadPoolTests"):
        return "thread helpers"
    if "LoggingSystem" in n:
        return "logging system"
    if n.endswith(("Msg", "Warning", "Error")) or n in ("ConMsg", "ConDMsg", "CoreMsgV", "Msg", "Warning", "Error", "DevMsg",
                                                        "DevWarning", "Plat_SimpleLog", "COM_TimestampedLog") \
            or "SpewCallStack" in n or "AlwaysSpewCallStack" in n or n.startswith(("Print_", "SetErrorPrintCallback", "ErrorCount", "ActionableError")):
        return "spew, errors and asserts"
    if "Assert" in n or n in ("ShouldUseNewAssertDialog", "DoNewAssertDialog", "_ExitOnFatalAssert", "CrackSmokingCompiler"):
        return "spew, errors and asserts"
    if n.startswith("Plat_") and any(k in n for k in ("Window", "Desktop", "Shell", "ScreenTo", "MessageBox")):
        return "window and desktop"
    if n in ("InitWin32ConsoleColorContext", "RestoreWin32ConsoleColor", "SetWin32ConsoleColor", "SetupWin32ConsoleIO"):
        return "console"
    if n in ("CommandLine", "Plat_GetCommandLine", "Plat_GetCommandLineA"):
        return "command line"
    if n in ("Plat_FloatTime", "Plat_MSTime", "Timer_GetTimeUS", "Plat_GetClockStart", "Plat_GetLocalTime", "Plat_gmtime",
             "Plat_ConvertToLocalTime", "Plat_GetTimeString", "GetCurrentDate", "GetCurrentDayOfTheWeek",
             "GetCurrentDayOfTheYear") or n.startswith("g_ClockSpeed") or n == "g_dwClockSpeed":
        return "timing and date"
    if n in ("GetCPUInformation", "InitPME", "ShutdownPME", "vtune", "GetFreeMemory", "GetTotalMemory",
             "Plat_GetMemPageSize", "Plat_GetPagedPoolInfo", "Plat_GetOSVersion"):
        return "CPU, OS and machine information"
    if n in ("CreateGlobalMemAlloc", "MemAllocScratch", "MemFreeScratch", "g_pMemAllocSingleton"):
        return "memory allocator"
    if n.startswith("Tier0_") or n.startswith("Origin"):
        return "Origin (EA) glue"
    if n in ("DetectLanguage", "GetLanguageName"):
        return "language"
    if n in ("EncodeBinaryToString", "DecodeBinaryFromString"):
        return "string utilities"
    if n in ("Plat_ExitProcess", "Plat_DebugString", "Plat_GetModuleFilename", "Plat_GetProcAddress",
             "Plat_IsInDebugSession", "Plat_IsInBenchmarkMode", "Plat_IsInRenderToolMode", "Plat_IsInToolMode",
             "Plat_SetBenchmarkMode", "Plat_SetRenderToolMode", "Plat_SetToolMode"):
        return "process and platform"
    if "Breakpoint" in n:
        return "debugging"
    if any(k in n for k in ("CallStack", "Symbol", "FileAndLine", "ModuleNameFromAddress", "TranslateStackInfo",
                            "StackTools", "StackTranslation")):
        return "call-stack capture and stats"
    if "MiniProfiler" in n or n in ("PublishAll", "PublishAllMiniProfilers"):
        return "mini-profiler"
    if "Remote" in n or "KitOutside" in n or "Pak_" in n or "g_pakLoadApi" in n:
        return "tools, remote error reports and pak glue"
    # vstdlib
    if "Random" in n:
        return "random numbers"
    if n.startswith("V_"):
        return "string utilities"
    if n.startswith("Coroutine_"):
        return "coroutines"
    if n in ("CreateInterface", "VStdLib_GetICVarFactory"):
        return "interface factory"
    if "KeyValues" in n:
        return "key-values"
    if "ThreadPool" in n or n == "g_pThreadPool":
        return "thread pool"
    if n == "g_VCoverage":
        return "misc data"
    return "other"


if __name__ == "__main__":
    for mname in MODULES:
        mm = load(mname)
        print(mname, len(mm.exports), "exports;", sum(1 for e in mm.exports if e["kind"] == "function"), "functions")
