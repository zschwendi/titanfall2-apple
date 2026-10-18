#!/usr/bin/env python3
"""Scout for issue #28: how does the game use exceptions, concretely?

Static analysis only (the DLLs are never copied or executed). For each x64 module:

 1. every distinct handler routine named by the unwind tables is identified by disassembling it
    (or by the import it thunks to), not by the data that follows it;
 2. C++ EH functions are parsed through FuncInfo: real try/catch blocks versus unwind-only states;
 3. SEH functions are parsed through the C scope table: __except filters versus __finally;
 4. _CxxThrowException is located by structure (the 0xE06D7363 code and a call to RaiseException)
    and every call site is found and verified against an instruction boundary; RaiseException
    call sites outside it are listed with the exception code where it is a constant;
 5. handler-bearing functions are hashed (call and rip-relative displacements masked) so that
    functions that are byte-identical across modules (the static CRT) can be told from the rest:
    tier A = same hash in 12 or more of the 16 first-party DLLs, B = 5-11, C = 2-4, D = one DLL.

Phase 1 (`analyze`) writes one JSON per module into --cache (about 3 s with 4 processes);
phase 2 (`report`) aggregates those into Markdown tables (`--split DIR` writes one file per table).

    .venv/bin/python tools/scout/exceptions.py analyze game --cache /path/to/cache [-j 4]
    .venv/bin/python tools/scout/exceptions.py report --cache /path/to/cache
"""

import argparse
import bisect
import hashlib
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))
from pe_inventory import PE, find_pe_files  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

CXX_EXC_CODE = 0xE06D7363           # the MSVC C++ exception code ('msc' | 0xE0000000)
EH_MAGICS = (0x19930520, 0x19930521, 0x19930522)

# Imports whose call sites we count. Anything the exception machinery or a crash handler touches.
API_NAMES = [
    "RaiseException", "RtlUnwindEx", "RtlUnwind", "RtlVirtualUnwind", "RtlLookupFunctionEntry",
    "RtlCaptureContext", "RtlPcToFileHeader", "RtlRestoreContext", "RtlAddFunctionTable",
    "RtlInstallFunctionTableCallback", "RtlDeleteFunctionTable", "SetUnhandledExceptionFilter",
    "UnhandledExceptionFilter", "AddVectoredExceptionHandler", "RemoveVectoredExceptionHandler",
    "IsDebuggerPresent", "DebugBreak", "TerminateProcess", "RaiseFailFastException",
    "_CxxThrowException", "__CxxFrameHandler3", "__C_specific_handler", "__crtUnhandledException",
    "?terminate@@YAXXZ", "_set_se_translator", "?set_terminate@@YAP6AXXZP6AXXZ@Z",
    "?_Xbad_alloc@std@@YAXXZ", "?_Xlength_error@std@@YAXPEBD@Z", "?_Xout_of_range@std@@YAXPEBD@Z",
    "?_Xinvalid_argument@std@@YAXPEBD@Z", "?_Xruntime_error@std@@YAXPEBD@Z",
    "?_Xoverflow_error@std@@YAXPEBD@Z", "signal", "abort",
]

EXEC = 0x20000000 | 0x20


def u32(d, off):
    return struct.unpack_from("<I", d, off)[0]


class Mod:
    def __init__(self, path):
        self.pe = pe = PE(path)
        self.d = pe.data
        self.name = os.path.basename(path)
        self.base = pe.image_base
        self.secs = pe.sections
        self.exec_secs = [s for s in pe.sections if s["flags"] & EXEC]
        rva, size = pe.dirs[3]
        off = pe.rva_to_off(rva)
        self.entries = [struct.unpack_from("<III", self.d, off + 12 * i) for i in range(size // 12)]
        self.begins = [e[0] for e in self.entries]
        self.sorted = self.begins == sorted(self.begins)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self._cookie = None
        lc = pe.dirs[10]
        if lc[0]:
            o = pe.rva_to_off(lc[0])
            if o is not None and u32(self.d, o) >= 96:
                (va,) = struct.unpack_from("<Q", self.d, o + 88)
                self._cookie = va - self.base if va else None
        self._parse_unwind()
        self._parse_imports()
        self._feat = {}
        self._role = {}

    # ---- unwind table ----------------------------------------------------------------------

    def _parse_unwind(self):
        d, pe = self.d, self.pe
        self.info = []
        self.indirect = 0
        for b, e, u in self.entries:
            if u & 1:
                self.indirect += 1
            uoff = pe.rva_to_off(u & ~1)
            ver_flags, _, ncodes, _ = struct.unpack_from("<BBBB", d, uoff)
            flags = ver_flags >> 3
            after = uoff + 4 + 2 * ((ncodes + 1) & ~1)
            rec = {"flags": flags, "chain": None, "handler": None, "hdata": None}
            if flags & 4:
                rec["chain"] = struct.unpack_from("<III", d, after)[0]       # parent BeginAddress
            elif flags & 3:
                rec["handler"] = u32(d, after)
                rec["hdata"] = after + 4
            self.info.append(rec)

    def idx_at(self, rva):
        i = bisect.bisect_right(self.begins, rva) - 1
        if i >= 0 and self.entries[i][0] <= rva < self.entries[i][1]:
            return i
        return None

    def root_of(self, i):
        for _ in range(16):
            c = self.info[i]["chain"]
            if c is None:
                return i
            j = self.idx_at(c)
            if j is None or j == i:
                return i
            i = j
        return i

    # ---- imports -----------------------------------------------------------------------------

    def _parse_imports(self):
        pe, d = self.pe, self.d
        self.iat = {}            # slot RVA -> (dll, name)
        rva, _ = pe.dirs[1]
        off = pe.rva_to_off(rva) if rva else None
        while off is not None and off + 20 <= len(d):
            oft, _, _, name, ft = struct.unpack_from("<IIIII", d, off)
            if not (oft or name or ft):
                break
            dll = (pe.cstr(name) or "?").lower()
            names = pe._thunks(oft or ft)
            for k, n in enumerate(names):
                self.iat[ft + 8 * k] = (dll, n)
            off += 20
        rva, _ = pe.dirs[13]
        off = pe.rva_to_off(rva) if rva else None
        while off is not None and off + 32 <= len(d):
            attrs, name, _, iat, int_rva, _, _, _ = struct.unpack_from("<IIIIIIII", d, off)
            if not (name or int_rva):
                break
            if attrs & 1:
                dll = (pe.cstr(name) or "?").lower()
                for k, n in enumerate(pe._thunks(int_rva)):
                    self.iat[iat + 8 * k] = (dll + " (delay)", n)
            off += 32
        self.slot_by_name = defaultdict(list)
        for s, (dll, n) in self.iat.items():
            self.slot_by_name[n].append(s)

    # ---- code access -----------------------------------------------------------------------

    def code(self, b, e):
        off = self.pe.rva_to_off(b)
        return self.d[off:off + (e - b)] if off is not None else b""

    def disasm(self, b, e):
        return list(self.md.disasm(self.code(b, e), b))

    def thunk_import(self, rva, depth=0):
        """If the code at rva is `jmp [iat]` (possibly after a jmp rel32), return (dll, name)."""
        off = self.pe.rva_to_off(rva)
        if off is None or depth > 3:
            return None
        b = self.d[off:off + 6]
        if b[:2] == b"\xff\x25":
            disp = struct.unpack_from("<i", b, 2)[0]
            return self.iat.get(rva + 6 + disp)
        if b[:1] == b"\xe9":
            rel = struct.unpack_from("<i", b, 1)[0]
            return self.thunk_import(rva + 5 + rel, depth + 1)
        return None

    # ---- instruction features ----------------------------------------------------------------

    def feat(self, b, e):
        key = (b, e)
        if key in self._feat:
            return self._feat[key]
        imms, calls, iat = set(), set(), Counter()
        cookie = rep = False
        rol16 = cmp_rip = pushfq = False
        ctx = set()
        n = 0
        for ins in self.disasm(b, e):
            n += 1
            mn = ins.mnemonic
            if mn.startswith("rep movs"):
                rep = True
            elif mn == "pushfq":
                pushfq = True
            elif mn == "rol" and len(ins.operands) == 2 and ins.operands[1].type == X.X86_OP_IMM \
                    and ins.operands[1].imm == 0x10:
                rol16 = True
            for op in ins.operands:
                if op.type == X.X86_OP_IMM:
                    if mn in ("call", "jmp"):
                        if not (b <= op.imm < e):
                            calls.add(op.imm)
                    elif not mn.startswith("j"):
                        imms.add(op.imm & 0xFFFFFFFF)
                elif op.type == X.X86_OP_MEM:
                    if op.mem.base == X.X86_REG_RIP:
                        t = ins.address + ins.size + op.mem.disp
                        if mn in ("call", "jmp"):
                            nm = self.iat.get(t)
                            iat[nm[1] if nm else "?%x" % t] += 1
                        elif t == self._cookie:
                            cookie = True
                    elif op.mem.base == X.X86_REG_R8 and op.mem.disp in (0x78, 0xF8):
                        ctx.add(op.mem.disp)
                    if op.mem.base == X.X86_REG_RIP and mn == "cmp":
                        cmp_rip = True
        f = {"imms": imms, "calls": calls, "iat": iat, "cookie": cookie or (rol16 and cmp_rip), "rep": rep, "n": n,
             "size": e - b, "pushfq": pushfq, "ctx": ctx}
        self._feat[key] = f
        return f

    def extent(self, rva):
        """(begin, end) of the routine at rva: the unwind entry if it starts there, otherwise the
        straight-line code up to the first ret (hand-written leaf routines have no entry)."""
        i = self.idx_at(rva)
        if i is not None and self.entries[i][0] == rva:
            return rva, self.entries[i][1]
        end = rva
        for ins in self.disasm(rva, rva + 0x300):
            end = ins.address + ins.size
            if ins.mnemonic in ("ret", "repz ret", "rep ret"):
                break
        return rva, end

    def cookie_reach(self, rva, depth=2):
        """Does the routine at rva touch the security cookie, directly or in a callee?"""
        b, e = self.extent(rva)
        f = self.feat(b, e)
        if f["cookie"]:
            return True
        if depth == 0:
            return False
        return any(self.cookie_reach(c, depth - 1) for c in f["calls"] if self.pe.section_of(c) is not None)

    # ---- handler identification ---------------------------------------------------------------

    def role(self, h):
        """Identify a handler routine by what it is. Returns (role, evidence string)."""
        if h not in self._role:
            self._role[h] = ("unidentified", "cycle")      # guard against call-graph cycles
            self._role[h] = self._role_uncached(h)
        return self._role[h]

    def atom(self, rva):
        """Non-recursive classification of a callee: thunk import name, or a CRT atom by features."""
        t = self.thunk_import(rva)
        if t:
            return t[1]
        if self.idx_at(rva) is None:
            return None
        b, e = self.extent(rva)
        f = self.feat(b, e)
        imms, iat = f["imms"], f["iat"]
        magic = [m for m in EH_MAGICS if m in imms]
        if magic and 0x1FFFFFFF in imms:
            return "__InternalCxxFrameHandler"
        if CXX_EXC_CODE in imms and "RtlUnwindEx" in iat and not magic:
            return "__C_specific_handler"
        if f["size"] < 0x200 and not f["cookie"] and len(f["calls"]) <= 2 \
                and any(self.atom_shallow(c) == "__InternalCxxFrameHandler" for c in f["calls"]):
            return "__CxxFrameHandler3"
        return None

    def atom_shallow(self, rva):
        if self.idx_at(rva) is None:
            return None
        b, e = self.extent(rva)
        imms = self.feat(b, e)["imms"]
        if any(m in imms for m in EH_MAGICS) and 0x1FFFFFFF in imms:
            return "__InternalCxxFrameHandler"
        return None

    def _role_uncached(self, h):
        t = self.thunk_import(h)
        if t:
            return t[1], "jmp thunk to import %s!%s" % t
        b, e = self.extent(h)
        f = self.feat(b, e)
        imms, iat = f["imms"], f["iat"]
        if (f["rep"] and "RtlVirtualUnwind" in iat and 0x9A in imms) or (f["pushfq"] and f["ctx"] == {0x78, 0xF8}):
            return "asm_se_handler", ("hand-written leaf handler: reads CONTEXT.Rax/Rip, pushfq, no compiler prologue "
                                      "(copies a CONTEXT and calls RtlVirtualUnwind, possibly in a shared tail)")
        a = self.atom(h)
        if a == "__InternalCxxFrameHandler":
            return a, "tests the FuncInfo magic 0x1993052x and mask 0x1fffffff"
        if a == "__C_specific_handler":
            return a, "walks a scope table, tests code 0xE06D7363, calls RtlUnwindEx"
        if a == "__CxxFrameHandler3":
            return a, "loads FuncInfo from HandlerData, calls __InternalCxxFrameHandler"
        callees = [c for c in f["calls"] if self.pe.section_of(c) is not None]
        if f["size"] < 0x200 and any(self.cookie_reach(c) for c in callees if not self.thunk_import(c)):
            atoms = [self.atom(c) for c in callees]
            has_cxx = "__CxxFrameHandler3" in atoms
            has_seh = "__C_specific_handler" in atoms
            if has_cxx and not has_seh:
                return "__GSHandlerCheck_EH", "GS check (reaches the security cookie), then chains to __CxxFrameHandler3"
            if has_seh and not has_cxx:
                return "__GSHandlerCheck_SEH", "GS check (reaches the security cookie), then chains to __C_specific_handler"
            if not has_cxx and not has_seh:
                return "__GSHandlerCheck", "GS check only (reaches the security cookie), returns ContinueSearch"
        return "unidentified", "size %d, %d insns, calls %d, imm %s" % (
            f["size"], f["n"], len(f["calls"]), sorted(hex(x) for x in imms if x > 0xFFFF)[:4])

    # ---- C++ EH data ----------------------------------------------------------------------------

    def typename(self, td):
        s = self.pe.cstr(td + 16, 256)
        if not s:
            return "?"
        m = re.match(r"\.\?A[VU](.*)", s)
        if not m:
            return s
        body = m.group(1)
        if body.startswith("?$"):
            return "template " + body[:60]
        return "::".join(reversed([p for p in body.split("@") if p]))

    def parse_funcinfo(self, rva):
        d, pe = self.d, self.pe
        off = pe.rva_to_off(rva)
        if off is None or off + 40 > len(d):
            return None
        magic, maxs, dunw, ntry, dtry, nip, dip, duh, des, ehf = struct.unpack_from("<iiiIiIiiii", d, off)
        if (magic & 0x1FFFFFFF) not in EH_MAGICS or maxs < 0 or maxs > 20000 or ntry > 2000:
            return None
        fi = {"maxstate": maxs, "ntry": ntry, "actions": 0, "es": bool(des), "ehflags": ehf,
              "catches": [], "catch_funclets": [], "cleanup_funclets": [], "bad": False, "dotdot": 0}
        uo = pe.rva_to_off(dunw) if dunw else None
        if maxs and uo is not None:
            for k in range(maxs):
                if uo + 8 * k + 8 > len(d):
                    fi["bad"] = True
                    break
                _, act = struct.unpack_from("<ii", d, uo + 8 * k)
                if act:
                    fi["actions"] += 1
                    fi["cleanup_funclets"].append(act)
        to = pe.rva_to_off(dtry) if dtry else None
        for k in range(ntry):
            if to is None:
                fi["bad"] = True
                break
            tl, th, ch, nc, dh = struct.unpack_from("<iiiii", d, to + 20 * k)
            ho = pe.rva_to_off(dh)
            if ho is None or nc > 64:
                fi["bad"] = True
                continue
            for j in range(nc):
                adj, dtype, dobj, dhandler, dframe = struct.unpack_from("<iiiii", d, ho + 20 * j)
                sec = pe.section_of(dhandler)
                if sec is None or not (sec["flags"] & EXEC):
                    fi["bad"] = True
                tn = "..." if dtype == 0 else self.typename(dtype)
                if adj & 0x40:
                    fi["dotdot"] += 1
                fi["catches"].append(tn)
                fi["catch_funclets"].append(dhandler)
        return fi

    def parse_scope(self, hdata, role):
        d = self.d
        n = u32(d, hdata)
        if n > 5000 or hdata + 4 + 16 * n > len(d):
            return None
        return [struct.unpack_from("<IIII", d, hdata + 4 + 16 * k) for k in range(n)]

    def loads_exception_template(self, b, e):
        """Does the routine load from a data block whose first dword is the C++ exception code?"""
        for ins in self.disasm(b, e):
            for op in ins.operands:
                if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                    t = ins.address + ins.size + op.mem.disp
                    sec = self.pe.section_of(t)
                    if sec is not None and not (sec["flags"] & EXEC):
                        off = self.pe.rva_to_off(t)
                        if off is not None and u32(self.d, off) == CXX_EXC_CODE:
                            return True
        return False

    # ---- hashing ---------------------------------------------------------------------------------

    def func_hash(self, b, e):
        code = bytearray(self.code(b, e))
        n = 0
        for ins in self.md.disasm(bytes(code), b):
            n += 1
            o = ins.address - b
            rip = any(op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP for op in ins.operands)
            if rip and ins.disp_size:
                code[o + ins.disp_offset:o + ins.disp_offset + ins.disp_size] = b"\0" * ins.disp_size
            elif ins.mnemonic in ("call", "jmp") and ins.imm_size == 4:
                tgt = ins.operands[0].imm
                if not (b <= tgt < e):
                    code[o + ins.imm_offset:o + ins.imm_offset + 4] = b"\0\0\0\0"
        return hashlib.sha1(bytes(code)).hexdigest()[:16], n

    # ---- call-site search ------------------------------------------------------------------------

    def find_sites(self, targets, slots):
        """Raw byte search for call/jmp rel32 to `targets` (RVAs) and call/jmp [rip+x] to IAT `slots`.
        Returns candidate site RVAs with (kind, target)."""
        out = []
        pat = re.compile(rb"[\xe8\xe9]|\xff[\x15\x25]", re.S)
        for s in self.exec_secs:
            size = min(s["rsize"], s["vsize"])
            raw = self.d[s["rptr"]:s["rptr"] + size]
            va = s["va"]
            for m in pat.finditer(raw):
                p = m.start()
                if raw[p] == 0xFF:
                    if p + 6 > size:
                        continue
                    disp = struct.unpack_from("<i", raw, p + 2)[0]
                    slot = va + p + 6 + disp
                    if slot in slots:
                        out.append((va + p, "call" if raw[p + 1] == 0x15 else "jmp", slot))
                else:
                    if p + 5 > size:
                        continue
                    rel = struct.unpack_from("<i", raw, p + 1)[0]
                    t = va + p + 5 + rel
                    if t in targets:
                        out.append((va + p, "call" if raw[p] == 0xE8 else "jmp", t))
        return out

    def verify_sites(self, cands):
        """Keep candidates that fall on an instruction boundary of a disassembled unwind-table
        range. Returns (verified list of (site, kind, target, func_index, insns_before), n_rejected)."""
        by_func = defaultdict(list)
        loose = []
        for c in cands:
            i = self.idx_at(c[0])
            if i is None:
                loose.append(c)
            else:
                by_func[i].append(c)
        ok, rejected = [], 0
        for i, cs in by_func.items():
            b, e, _ = self.entries[i]
            insns = self.disasm(b, e)
            addr = {ins.address: k for k, ins in enumerate(insns)}
            for site, kind, tgt in cs:
                k = addr.get(site)
                if k is None or insns[k].mnemonic != kind:
                    rejected += 1
                    continue
                ok.append((site, kind, tgt, i, insns[max(0, k - 14):k]))
        return ok, rejected, loose


def lea_target(insns, reg_ids):
    """Last `lea reg, [rip+x]` before the end of insns for one of reg_ids: its target RVA."""
    for ins in reversed(insns):
        if ins.mnemonic == "lea" and ins.operands[0].type == X.X86_OP_REG and ins.operands[0].reg in reg_ids:
            op = ins.operands[1]
            if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                return ins.address + ins.size + op.mem.disp
            return None
        if ins.operands and ins.operands[0].type == X.X86_OP_REG and ins.operands[0].reg in reg_ids:
            return None             # overwritten some other way
    return None


def last_imm(insns, reg_ids):
    for ins in reversed(insns):
        if ins.operands and ins.operands[0].type == X.X86_OP_REG and ins.operands[0].reg in reg_ids \
                and ins.mnemonic not in ("cmp", "test"):
            if ins.mnemonic == "mov" and len(ins.operands) == 2 and ins.operands[1].type == X.X86_OP_IMM:
                return ins.operands[1].imm & 0xFFFFFFFF
            if ins.mnemonic == "xor" and len(ins.operands) == 2 and ins.operands[1].type == X.X86_OP_REG \
                    and ins.operands[1].reg == ins.operands[0].reg:
                return 0
            return None
    return None


RCX = {X.X86_REG_ECX, X.X86_REG_RCX}
RDX = {X.X86_REG_RDX, X.X86_REG_EDX}
R8 = {X.X86_REG_R8D, X.X86_REG_R8}


def throw_type(m, insns):
    """Resolve the ThrowInfo passed in rdx before a _CxxThrowException call to the thrown type."""
    ti = lea_target(insns, RDX)
    if ti is None:
        return None
    d, pe = m.d, m.pe
    off = pe.rva_to_off(ti)
    if off is None or off + 16 > len(d):
        return None
    _, _, _, cta = struct.unpack_from("<IIII", d, off)
    co = pe.rva_to_off(cta)
    if co is None:
        return None
    n = u32(d, co)
    if not 0 < n < 64:
        return None
    ct = u32(d, co + 4)
    cto = pe.rva_to_off(ct)
    if cto is None:
        return None
    td = u32(d, cto + 4)
    return m.typename(td)


def analyze(path):
    m = Mod(path)
    pe = m.pe
    res = {"name": m.name, "linker": pe.linker, "entries": len(m.entries), "sorted": m.sorted,
           "indirect_unwind": m.indirect, "entry_rva": pe.entry_rva}
    n = len(m.entries)
    chain = sum(1 for r in m.info if r["chain"] is not None)
    res["chain"] = chain
    res["primary"] = n - chain
    res["imports_dynamic_crt"] = sorted({dll for dll, _ in m.iat.values() if dll.startswith(("msvcr", "msvcp", "vcruntime", "api-ms"))})

    # ---- handler routines ---------------------------------------------------------------
    handler_fns = [i for i, r in enumerate(m.info) if r["handler"] is not None]
    by_h = defaultdict(list)
    for i in handler_fns:
        by_h[m.info[i]["handler"]].append(i)
    res["handler_fns"] = len(handler_fns)
    handlers = []
    for h, idxs in sorted(by_h.items(), key=lambda kv: -len(kv[1])):
        role, ev = m.role(h)
        ext = m.extent(h)
        thunk = ev.startswith("jmp thunk")
        handlers.append({"rva": h, "functions": len(idxs), "role": role, "evidence": ev,
                         "size": 6 if thunk else ext[1] - ext[0], "thunk": thunk,
                         "has_unwind_entry": m.idx_at(h) is not None})
    res["handlers"] = handlers
    role_of = {h["rva"]: h["role"] for h in handlers}

    # ---- per-function parse -----------------------------------------------------------------
    catch_funclets, cleanup_funclets, seh_funclets = set(), set(), set()
    funcs = []
    bad_parse = Counter()
    fi_cache = {}
    for i in handler_fns:
        r = m.info[i]
        b, e, _ = m.entries[i]
        role = role_of[r["handler"]]
        rec = {"rva": b, "size": e - b, "role": role}
        if role in ("__CxxFrameHandler3", "__GSHandlerCheck_EH"):
            fi_rva = u32(m.d, r["hdata"])
            fi = fi_cache.get(fi_rva)
            if fi is None and fi_rva not in fi_cache:
                fi = m.parse_funcinfo(fi_rva)
                fi_cache[fi_rva] = fi
            if fi is None:
                bad_parse[role] += 1
                rec["cxx"] = None
            else:
                catch_funclets.update(fi["catch_funclets"])
                cleanup_funclets.update(fi["cleanup_funclets"])
                if fi["bad"]:
                    bad_parse[role + " (inconsistent)"] += 1
                rec["cxx"] = {"fi": fi_rva, "maxstate": fi["maxstate"], "ntry": fi["ntry"],
                              "funclets": fi["catch_funclets"], "dotdot": fi["dotdot"],
                              "ncatch": len(fi["catches"]), "catches": fi["catches"],
                              "actions": fi["actions"], "es": fi["es"], "ehflags": fi["ehflags"]}
        elif role in ("__C_specific_handler", "__GSHandlerCheck_SEH"):
            sc = m.parse_scope(r["hdata"], role)
            if sc is None:
                bad_parse[role] += 1
                rec["seh"] = None
            else:
                ex, fin = [], 0
                for sb, se, sh, st in sc:
                    if st == 0:
                        fin += 1
                        if sh > 1:
                            seh_funclets.add(sh)
                    else:
                        ex.append(sh)
                        if sh > 1:
                            seh_funclets.add(sh)
                filters = []
                for sh in ex:
                    if sh == 1:
                        filters.append({"kind": "all"})
                    else:
                        fb, fe = m.extent(sh)
                        f = m.feat(fb, fe)
                        codes = sorted(hex(x) for x in f["imms"] if x >= 0x40000000 and x <= 0xE0FFFFFF and (x >> 28) in (4, 8, 0xC, 0xE))
                        filters.append({"kind": "funclet", "rva": sh, "size": fe - fb,
                                        "calls": len(f["calls"]), "iat": sorted(f["iat"]), "codes": codes})
                rec["seh"] = {"scopes": len(sc), "except": len(ex), "finally": fin, "filters": filters,
                              "except_ranges": [[sb, se, sh] for sb, se, sh, st in sc if st]}
        funcs.append(rec)
    # funclet flags
    for rec in funcs:
        b = rec["rva"]
        rec["funclet"] = ("catch" if b in catch_funclets else "cleanup" if b in cleanup_funclets
                          else "seh" if b in seh_funclets else None)
    res["bad_parse"] = dict(bad_parse)
    res["funclets"] = {"catch": len(catch_funclets), "cleanup": len(cleanup_funclets), "seh": len(seh_funclets)}

    # ---- hashes (also chained pieces' root is the function; only primaries hashed) ----------------
    for rec in funcs:
        h, ninsn = m.func_hash(rec["rva"], rec["rva"] + rec["size"])
        rec["hash"] = h
        rec["insns"] = ninsn
    res["funcs"] = funcs

    # ---- throw machinery -------------------------------------------------------------------------
    watch_names = [nm for nm in API_NAMES if nm in m.slot_by_name]
    slot_name = {}
    for nm in watch_names:
        for s_ in m.slot_by_name[nm]:
            slot_name[s_] = nm
    # STL throw helpers imported from a dynamic CRT (msvcp110): by name pattern
    for s_, (dll, nm) in m.iat.items():
        if re.match(r"\?_X[a-z_]+@std@@", nm) and s_ not in slot_name:
            slot_name[s_] = nm
    # stubs: a bare `jmp [slot]` that no unwind entry covers (calls to them are calls to the import)
    stubs = {}
    for site, kind, slot in m.find_sites(set(), set(slot_name)):
        if kind == "jmp" and (m.idx_at(site) is None):
            stubs[site] = slot_name[slot]
    # pass A: who calls RaiseException? The static _CxxThrowException is the small one that loads a
    # template exception record from data whose first dword is the code 0xE06D7363.
    raise_slots = {s_ for s_, nm in slot_name.items() if nm == "RaiseException"}
    raise_stubs = {a_ for a_, nm in stubs.items() if nm == "RaiseException"}
    okA, _, _ = m.verify_sites(m.find_sites(raise_stubs, raise_slots))
    throw_fn = []
    for root in sorted({m.root_of(i) for _, _, _, i, _ in okA}):
        b_, e_, _ = m.entries[root]
        f = m.feat(b_, e_)
        if f["size"] < 0x300 and m.loads_exception_template(b_, e_):
            throw_fn.append(b_)
    # for the record: where the code constant appears at all
    hits = 0
    for sec in m.exec_secs:
        size = min(sec["rsize"], sec["vsize"])
        hits += len(re.findall(rb"\x63\x73\x6d\xe0", m.d[sec["rptr"]:sec["rptr"] + size]))
    res["cxx_code_hits"] = hits
    res["throw_fn"] = throw_fn
    res["throw_import"] = any(nm == "_CxxThrowException" for nm in slot_name.values())

    # pass B: every call site of every watched target
    targets = {t: "_CxxThrowException" for t in throw_fn}
    targets.update(stubs)
    cands = m.find_sites(set(targets), set(slot_name))
    ok, rejected, loose = m.verify_sites(cands)
    res["site_rejected"] = rejected
    res["site_unverified"] = len([c for c in loose if c[0] not in stubs])
    # call sites in hand-written code that no unwind entry covers (the asm handlers)
    api_loose = Counter()
    for site, kind, tgt in loose:
        if site not in stubs and kind == "call":
            api_loose[slot_name.get(tgt) or targets.get(tgt)] += 1
    res["api_calls_outside_unwind"] = dict(api_loose)

    api = Counter()
    throws, raises = [], []
    for site, kind, tgt, i, before in ok:
        nm = slot_name.get(tgt) or targets.get(tgt)
        root = m.root_of(i)
        fb = m.entries[root][0]
        in_throw_fn = fb in throw_fn          # the throw routine's own RaiseException call
        if not in_throw_fn:
            api[nm] += 1
        size = m.entries[root][1] - fb
        if nm == "_CxxThrowException":
            rethrow = last_imm(before, RCX) == 0 and last_imm(before, RDX) == 0
            throws.append({"site": site, "kind": kind, "func": fb, "size": size,
                           "type": "(rethrow)" if rethrow else throw_type(m, before), "rethrow": rethrow})
        elif nm and nm.startswith("?_X"):
            throws.append({"site": site, "kind": kind, "func": fb, "size": size, "type": nm, "via": "import"})
        if nm == "RaiseException" and not in_throw_fn:
            raises.append({"site": site, "func": fb, "size": size,
                           "code": last_imm(before, RCX), "nargs": last_imm(before, R8)})
    # catch funclets: does the handler rethrow (cleanup-and-rethrow) or swallow?
    rethrow_in = Counter(t["func"] for t in throws if t.get("rethrow"))
    throw_in = Counter(t["func"] for t in throws if not t.get("rethrow"))
    for rec in funcs:
        if rec["funclet"] == "catch":
            rec["rethrows"] = rethrow_in.get(rec["rva"], 0)
            rec["throws_new"] = throw_in.get(rec["rva"], 0)
    dbg_roots = {m.entries[m.root_of(i)][0] for site, kind, tgt, i, _ in ok if slot_name.get(tgt) == "IsDebuggerPresent"}
    raise_roots = {t["func"] for t in raises}
    exc_ranges = {rec["rva"]: rec["seh"]["except_ranges"] for rec in funcs if rec.get("seh")}
    for t in raises:
        t["dbgcheck"] = t["func"] in dbg_roots
        t["in_except"] = any(sb <= t["site"] < se for sb, se, sh in exc_ranges.get(t["func"], []))
        t["except_all"] = any(sb <= t["site"] < se and sh == 1 for sb, se, sh in exc_ranges.get(t["func"], []))
    for rec in funcs:
        rec["raises"] = rec["rva"] in raise_roots
    res["api_calls"] = dict(api)
    res["throws"] = throws
    res["raises"] = raises
    # hash the functions that contain throws / raises
    ctx = {}
    for t in throws + raises:
        fb = t["func"]
        if fb not in ctx:
            i = m.idx_at(fb)
            ctx[fb] = m.func_hash(fb, m.entries[i][1])[0]
        t["hash"] = ctx[fb]
    # throw-routine location & imports of interest
    res["api_imports"] = sorted(nm for nm in watch_names)
    # number of callers of each function that contains a throw (to see through STL throw helpers)
    helper_targets = {t["func"] for t in throws}
    callers = Counter()
    if helper_targets:
        for site, kind, tgt in m.find_sites(helper_targets, set()):
            i = m.idx_at(site)
            if i is not None:
                callers[tgt] += 1
    res["helper_callers"] = {str(k): v for k, v in callers.items()}
    return res



# ---------------------------------------------------------------------------------------------------
# Phase 2: aggregate the per-module JSON into Markdown tables.
# ---------------------------------------------------------------------------------------------------

FIRST_PARTY = ["client.dll", "server.dll", "engine.dll", "materialsystem_dx11.dll", "vguimatsurface.dll",
               "vphysics.dll", "vgui2.dll", "filesystem_stdio.dll", "datacache.dll", "launcher.dll",
               "studiorender.dll", "localize.dll", "vstdlib.dll", "tier0.dll", "inputsystem.dll", "rtech_game.dll"]
THIRD_PARTY = ["OriginSDK.dll", "mileswin64.dll", "GFSDK_SSAO.win64.dll", "bink2w64.dll", "auditionwin64.dll",
               "amd_ags_x64.dll", "binkawin64.dll", "GFSDK_TXAA.win64.dll"]
EXTRA = ["d3dcompiler_46.dll", "ui(11).dll"]
ROLES = ["__GSHandlerCheck", "__GSHandlerCheck_EH", "__GSHandlerCheck_SEH", "__C_specific_handler",
         "__CxxFrameHandler3", "asm_se_handler"]
CXX_ROLES = ("__CxxFrameHandler3", "__GSHandlerCheck_EH")
SEH_ROLES = ("__C_specific_handler", "__GSHandlerCheck_SEH")
TIER_NAMES = {"A": "in >=12 of 16 DLLs", "B": "in 5-11 DLLs", "C": "in 2-4 DLLs", "D": "this DLL only"}


def md(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def short(n):
    return n.replace(".dll", "")


def load_cache(cache):
    R = {}
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        path = os.path.join(cache, n + ".json")
        if os.path.exists(path):
            with open(path) as f:
                R[n] = json.load(f)
    return R


class Tiers:
    def __init__(self, R):
        self.R = R
        self.mods = defaultdict(set)
        for n in FIRST_PARTY:
            if n not in R:
                continue
            for fn in R[n]["funcs"]:
                self.mods[fn["hash"]].add(n)
            for t in R[n]["throws"] + R[n]["raises"]:
                self.mods[t["hash"]].add(n)
        self.parent = {}
        for n, r in R.items():
            for fn in r["funcs"]:
                x = fn.get("cxx")
                if x and x["ntry"] > 0:
                    for fl in x["funclets"]:
                        self.parent[(n, fl)] = fn
        self.byrva = {n: {fn["rva"]: fn for fn in r["funcs"]} for n, r in R.items()}

    def of_hash(self, h):
        k = len(self.mods.get(h, ()))
        return "A" if k >= 12 else "B" if k >= 5 else "C" if k >= 2 else "D"

    def of_func(self, n, fn):
        if fn.get("funclet") == "catch":
            p = self.parent.get((n, fn["rva"]))
            if p is not None:
                fn = p
        return self.of_hash(fn["hash"])


def cxx_kind(fn):
    x = fn["cxx"]
    if x is None:
        return "unparsed"
    if x["ntry"] > 0:
        return "trycatch"
    if x["actions"] > 0:
        return "unwind"
    if x["maxstate"] > 0:
        return "states"
    return "none"


def section_handlers(R):
    rows = []
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        if n not in R:
            continue
        r = R[n]
        c = Counter(fn["role"] for fn in r["funcs"])
        other = sum(v for k, v in c.items() if k not in ROLES)
        rows.append([n, r["linker"], r["entries"], r["primary"], r["chain"], r["handler_fns"], len(r["handlers"])]
                    + [c.get(k, 0) for k in ROLES] + [other])
    tot = ["**total (24 DLLs)**", "", sum(x[2] for x in rows if x[0] in FIRST_PARTY + THIRD_PARTY),
           sum(x[3] for x in rows if x[0] in FIRST_PARTY + THIRD_PARTY),
           sum(x[4] for x in rows if x[0] in FIRST_PARTY + THIRD_PARTY),
           sum(x[5] for x in rows if x[0] in FIRST_PARTY + THIRD_PARTY),
           sum(x[6] for x in rows if x[0] in FIRST_PARTY + THIRD_PARTY)]
    for k in range(len(ROLES) + 1):
        tot.append(sum(x[7 + k] for x in rows if x[0] in FIRST_PARTY + THIRD_PARTY))
    rows.append(tot)
    return md(["Module", "Linker", "Unwind entries", "Primary", "Chained", "With handler", "Distinct handlers",
               "GSHandlerCheck", "GSHandlerCheck_EH", "GSHandlerCheck_SEH", "C_specific_handler", "CxxFrameHandler3",
               "asm handler", "other"], rows)


def section_catalogue(R):
    by = defaultdict(lambda: {"routines": 0, "fns": 0, "sizes": Counter(), "ex": [], "ev": None, "mods": set()})
    for n in FIRST_PARTY + THIRD_PARTY:
        if n not in R:
            continue
        for h in R[n]["handlers"]:
            d = by[h["role"]]
            d["routines"] += 1
            d["fns"] += h["functions"]
            d["sizes"][h["size"]] += 1
            d["mods"].add(n)
            if len(d["ex"]) < 3 and n in ("client.dll", "engine.dll", "materialsystem_dx11.dll", "OriginSDK.dll",
                                           "GFSDK_SSAO.win64.dll"):
                d["ex"].append("%s@0x%x (%d B)" % (short(n), h["rva"], h["size"]))
            if d["ev"] is None or n == "client.dll":
                d["ev"] = h["evidence"]
    rows = []
    for role in ROLES + sorted(k for k in by if k not in ROLES):
        d = by.get(role)
        if not d:
            continue
        sizes = ", ".join("%d" % s for s, _ in d["sizes"].most_common(4))
        rows.append([role, d["routines"], d["fns"], len(d["mods"]), sizes, d["ev"], "; ".join(d["ex"])])
    return md(["Routine", "Distinct routines (24 DLLs)", "Functions using it", "DLLs", "Sizes (bytes; 6 = import thunk)",
               "How it was recognised", "Examples"], rows)


def section_cxx(R, T):
    rows = []
    tot = Counter()
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        if n not in R:
            continue
        r = R[n]
        fns = [fn for fn in r["funcs"] if fn["role"] in CXX_ROLES]
        if not fns:
            continue
        c = Counter()
        tryb = ncatch = dots = swallow_f = rethrow_f = 0
        for fn in fns:
            if fn["funclet"] == "catch":
                c["funclets"] += 1
                if fn.get("rethrows"):
                    rethrow_f += 1
                else:
                    swallow_f += 1
                continue
            k = cxx_kind(fn)
            c[k] += 1
            x = fn["cxx"]
            tryb += x["ntry"]
            ncatch += x["ncatch"]
            dots += x["catches"].count("...")
        row = [n, len(fns), c["funclets"], len(fns) - c["funclets"], c["trycatch"], c["unwind"], c["states"],
               c["none"], tryb, ncatch, dots, ncatch - dots, rethrow_f, swallow_f]
        rows.append(row)
        if n not in EXTRA:
            for i, v in enumerate(row[1:], 1):
                tot[i] += v
    rows.append(["**total (24 DLLs)**"] + [tot[i] for i in range(1, 14)])
    return md(["Module", "C++ EH unwind entries", "of which catch funclets", "function bodies", "with try/catch",
               "unwind only (destructor actions)", "states, no actions", "no states", "try blocks", "catch handlers",
               "catch(...)", "typed catch", "funclets that rethrow", "funclets that do not"], rows)


def section_seh(R, T):
    rows = []
    tot = Counter()
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        if n not in R:
            continue
        r = R[n]
        fns = [fn for fn in r["funcs"] if fn["role"] in SEH_ROLES]
        if not fns:
            continue
        sc = ex = fin = fex = 0
        al = av = fl = 0
        tiers = Counter()
        for fn in fns:
            s = fn["seh"]
            sc += s["scopes"]
            ex += s["except"]
            fin += s["finally"]
            if s["except"]:
                fex += 1
                if n in FIRST_PARTY:
                    tiers[T.of_func(n, fn)] += 1
                for f in s["filters"]:
                    if f["kind"] == "all":
                        al += 1
                    else:
                        fl += 1
                        if "0xc0000005" in (f.get("codes") or []):
                            av += 1
        row = [n, len(fns), sc, ex, fin, fex, al, fl, av]
        row += [tiers.get(t, 0) if n in FIRST_PARTY else "-" for t in "ABCD"]
        rows.append(row)
        if n not in EXTRA:
            for i, v in enumerate(row[1:], 1):
                if isinstance(v, int):
                    tot[i] += v
    rows.append(["**total (24 DLLs)**"] + [tot[i] for i in range(1, 13)])
    return md(["Module", "SEH functions", "scope entries", "__except scopes", "__finally scopes",
               "functions with __except", "filter = catch-all", "filter = funclet", "filter tests 0xC0000005",
               "__except fns: tier A", "B", "C", "D"], rows)


def type_family(t):
    return t["type"] or "?"


def section_throws(R, T):
    rows = []
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        if n not in R:
            continue
        r = R[n]
        th = r["throws"]
        kind = "static" if r["throw_fn"] else ("import" if r["throw_import"] or any(t.get("via") == "import" for t in th) else "-")
        containers = {t["func"] for t in th}
        rethrow = sum(1 for t in th if t.get("rethrow"))
        new = [t for t in th if not t.get("rethrow")]
        rows.append([n, kind, len(th), len(containers), rethrow, len(new),
                     ", ".join("%s x%d" % (k, v) for k, v in Counter(type_family(t) for t in new).most_common(4)),
                     len(r["raises"]),
                     ", ".join(sorted({hex(t["code"]) for t in r["raises"] if t["code"] is not None}))])
    return md(["Module", "_CxxThrowException", "throw call sites", "containing functions", "of which rethrow (`throw;`)",
               "new throws", "types thrown (top)", "RaiseException sites outside it", "constant codes"], rows)


def section_tiers(R, T):
    rows = []
    for n in FIRST_PARTY:
        if n not in R:
            continue
        gs = 0
        cx, sh = Counter(), Counter()
        for fn in R[n]["funcs"]:
            if fn["funclet"] == "catch":
                continue
            if fn["role"] in CXX_ROLES:
                cx[T.of_func(n, fn)] += 1
            elif fn["role"] in SEH_ROLES:
                sh[T.of_func(n, fn)] += 1
            elif fn["role"] == "__GSHandlerCheck":
                gs += 1
            else:
                cx["asm"] += 1
        rows.append([n, gs, sum(sh.values()), "%d/%d/%d/%d" % tuple(sh.get(t, 0) for t in "ABCD"),
                     sum(v for k, v in cx.items() if k in "ABCD"), "%d/%d/%d/%d" % tuple(cx.get(t, 0) for t in "ABCD"),
                     cx.get("asm", 0)])
    tot = ["**total**", sum(r[1] for r in rows), sum(r[2] for r in rows), "", sum(r[4] for r in rows), "",
           sum(r[6] for r in rows)]
    rows.append(tot)
    return md(["Module", "Cookie-check only (GSHandlerCheck)", "SEH functions", "tier A/B/C/D",
               "C++ EH function bodies (no catch funclets)", "tier A/B/C/D", "hand-written asm handlers"], rows)


def section_api(R):
    tot_f, tot_t = Counter(), Counter()
    mods_f = defaultdict(set)
    for n in FIRST_PARTY + THIRD_PARTY:
        if n not in R:
            continue
        for k, v in R[n]["api_calls"].items():
            if n in FIRST_PARTY:
                tot_f[k] += v
                mods_f[k].add(n)
            else:
                tot_t[k] += v
        for k, v in R[n].get("api_calls_outside_unwind", {}).items():
            if n in FIRST_PARTY:
                tot_f[k + " (asm, outside unwind table)"] += v
                mods_f[k + " (asm, outside unwind table)"].add(n)
    rows = [[k, tot_f[k], len(mods_f[k]), tot_t.get(k, 0)] for k in sorted(set(tot_f) | set(tot_t))]
    return md(["Import", "call sites, 16 first-party DLLs", "DLLs", "call sites, 8 third-party DLLs"], rows)


def section_funclets(R):
    rows = []
    tot = Counter()
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        if n not in R:
            continue
        f = R[n]["funclets"]
        if not any(f.values()):
            continue
        rows.append([n, f["catch"], f["cleanup"], f["seh"], sum(f.values())])
        if n in FIRST_PARTY or n in THIRD_PARTY:
            for k in ("catch", "cleanup", "seh"):
                tot[k] += f[k]
    rows.append(["**total (24 DLLs)**", tot["catch"], tot["cleanup"], tot["seh"], sum(tot.values())])
    return md(["Module", "catch funclets", "cleanup funclets (destructor actions)", "SEH filter / __finally funclets",
               "total distinct funclets"], rows)


def section_appendix(R):
    rows = []
    for n in FIRST_PARTY + THIRD_PARTY + EXTRA:
        if n not in R:
            continue
        for h in sorted(R[n]["handlers"], key=lambda h: (-h["functions"], h["rva"])):
            rows.append([n, "0x%x" % h["rva"], h["size"],
                         "yes" if h["has_unwind_entry"] else ("no (import thunk)" if h.get("thunk") else "no"),
                         h["role"], h["functions"]])
    return md(["Module", "Handler RVA", "Bytes", "Has unwind entry", "Identified as", "Functions using it"], rows)


def section_residual(R, T):
    """Everything with exception semantics outside tier A, in the 16 first-party DLLs."""
    out = []
    rows = []
    for n in FIRST_PARTY:
        if n not in R:
            continue
        r = R[n]
        tc = [fn for fn in r["funcs"] if fn["role"] in CXX_ROLES and fn["funclet"] is None and cxx_kind(fn) == "trycatch"]
        tcc = Counter(T.of_func(n, fn) for fn in tc)
        swallow = []
        for fn in tc:
            fls = [T.byrva[n].get(f) for f in fn["cxx"]["funclets"]]
            if any(f is not None and not f.get("rethrows") for f in fls):
                swallow.append(fn)
        exc = [fn for fn in r["funcs"] if fn.get("seh") and fn["seh"]["except"]]
        excc = Counter(T.of_func(n, fn) for fn in exc)
        new = [t for t in r["throws"] if not t.get("rethrow")]
        newc = Counter(T.of_hash(t["hash"]) for t in new)
        newfn = {}
        for t in new:
            newfn.setdefault((t["func"], T.of_hash(t["hash"])), t)
        rows.append([n,
                     len(tc), "%d/%d/%d/%d" % tuple(tcc.get(t, 0) for t in "ABCD"), len(swallow),
                     len(exc), "%d/%d/%d/%d" % tuple(excc.get(t, 0) for t in "ABCD"),
                     len(new), "%d/%d/%d/%d" % tuple(newc.get(t, 0) for t in "ABCD"),
                     len(r["raises"])])
    return md(["Module", "functions with try/catch", "by tier A/B/C/D", "of which a catch swallows",
               "functions with __except", "by tier A/B/C/D", "new-throw sites", "by tier A/B/C/D",
               "RaiseException sites"], rows)


def list_residual(R, T, only=("C", "D")):
    lines = []
    for n in FIRST_PARTY:
        if n not in R:
            continue
        r = R[n]
        items = []
        for fn in sorted(r["funcs"], key=lambda f: f["rva"]):
            if fn["funclet"] is not None:
                continue
            t = T.of_func(n, fn)
            if t not in only:
                continue
            if fn["role"] in CXX_ROLES and cxx_kind(fn) == "trycatch":
                fls = [T.byrva[n].get(f) for f in fn["cxx"]["funclets"]]
                sw = any(f is not None and not f.get("rethrows") for f in fls)
                items.append("try/catch(%s) 0x%x/%dB tier %s%s" % (",".join(sorted(set(fn["cxx"]["catches"]))), fn["rva"], fn["size"], t, " SWALLOWS" if sw else ""))
            elif fn.get("seh") and fn["seh"]["except"]:
                f0 = fn["seh"]["filters"][0]
                items.append("__except 0x%x/%dB tier %s filter=%s%s" % (fn["rva"], fn["size"], t, f0["kind"], (" " + ",".join(f0.get("codes") or [])) if f0["kind"] != "all" else ""))
        for tt in sorted(r["throws"], key=lambda x: x["site"]):
            if tt.get("rethrow"):
                continue
            t = T.of_hash(tt["hash"])
            if t in only:
                items.append("throw %s in 0x%x/%dB tier %s" % (tt["type"], tt["func"], tt["size"], t))
        for tt in r["raises"]:
            t = T.of_hash(tt["hash"])
            if t in only:
                items.append("RaiseException(%s) in 0x%x/%dB tier %s" % (hex(tt["code"]) if tt["code"] is not None else "?", tt["func"], tt["size"], t))
        lines.append((n, items))
    return lines


def cmd_report(args):
    R = load_cache(args.cache)
    T = Tiers(R)
    parts = []

    def add(key, title, body):
        parts.append((key, "## %s\n\n%s\n" % (title, body)))

    add("T1", "1. Handler routines per module", section_handlers(R))
    add("T2", "2. Handler routine catalogue", section_catalogue(R))
    add("T3", "3. C++ EH (FuncInfo)", section_cxx(R, T))
    add("T3b", "3b. Funclets", section_funclets(R))
    add("T4", "4. SEH scope tables", section_seh(R, T))
    add("T5", "5. Throw and raise sites", section_throws(R, T))
    add("T6", "6. Boilerplate tiers (first-party DLLs)", section_tiers(R, T))
    add("T6b", "6b. Exception-related imports, call sites", section_api(R))
    add("T7", "7. Exception semantics by tier", section_residual(R, T))
    lines = []
    for n, items in list_residual(R, T, ("C", "D")):
        lines.append("- %s: %d items" % (n, len(items)))
        for it in items[:args.max_items]:
            lines.append("    - " + it)
        if len(items) > args.max_items:
            lines.append("    - ... %d more" % (len(items) - args.max_items))
    add("T8", "8. Outside tiers A and B (first-party)", "\n".join(lines))
    add("T9", "9. Appendix: every distinct handler routine", section_appendix(R))
    if args.split:
        os.makedirs(args.split, exist_ok=True)
        for key, text in parts:
            with open(os.path.join(args.split, key + ".md"), "w") as f:
                f.write(text)
    else:
        print("\n".join(t for _, t in parts))


def _worker(args):
    path, cache = args
    r = analyze(path)
    with open(os.path.join(cache, r["name"] + ".json"), "w") as f:
        json.dump(r, f)
    return r["name"], r["handler_fns"]


def cmd_analyze(args):
    os.makedirs(args.cache, exist_ok=True)
    paths = []
    for p in find_pe_files(args.root):
        base = os.path.basename(p)
        try:
            pe = PE(p)
        except Exception:
            continue
        if pe.machine != 0x8664 or not pe.dirs[3][0]:
            continue
        paths.append(p)
    if args.only:
        paths = [p for p in paths if os.path.basename(p).lower() in {x.lower() for x in args.only}]
    # unique names (the ui(NN).dll pak files share nothing with the others)
    jobs = [(p, args.cache) for p in paths]
    jobs.sort(key=lambda j: -os.path.getsize(j[0]))
    if args.j > 1:
        with Pool(min(args.j, 4)) as pool:
            for name, n in pool.imap_unordered(_worker, jobs):
                print("done", name, n, file=sys.stderr)
    else:
        for j in jobs:
            print("done", *_worker(j), file=sys.stderr)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("analyze")
    a.add_argument("root")
    a.add_argument("--cache", required=True)
    a.add_argument("-j", type=int, default=4)
    a.add_argument("--only", nargs="*")
    a.set_defaults(fn=cmd_analyze)
    r = sub.add_parser("report")
    r.add_argument("--cache", required=True)
    r.add_argument("--max-items", type=int, default=12)
    r.add_argument("--split", help="write each table to its own file in this directory")
    r.set_defaults(fn=cmd_report)
    args = ap.parse_args()
    args.fn(args)
