#!/usr/bin/env python3
"""Shared layer for the launcher-flow scout (finding 37).

Builds on threads_lib.Module (PE tables, .pdata function map, byte-level reference scans) and
adds what reading a start-up sequence needs:

  Mod(name)              one game DLL; `name` with or without ".dll"
  Mod.pieces(func)       every .pdata range that belongs to a function (chained unwind blocks)
  Mod.insns(func)        the function's instructions, all pieces, in address order
  Mod.thunks             {rva: (dll, name)} for the `jmp [import slot]` stubs
  Mod.call_target(ins)   classifies a call/jmp: import, direct, virtual slot, global pointer, reg
  Mod.vtable_class(rva)  RTTI class name of the vtable at rva (None when there is none)
  Mod.wstrings()         UTF-16 strings in the data sections
  Mod.label(rva)         export name, RTTI "Class::slotN", or "f<rva>"

Read-only on the game files.  OriginSDK.dll is refused by name: this layer never opens it.
Prints nothing by itself; the flow_*.py scripts print addresses, names of imports and exports,
strings the code refers to, and counts.
"""

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from threads_lib import Module  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

FORBIDDEN = {"originsdk.dll", "titanfall2.exe", "titanfall2_trial.exe"}
_RE_WIDE = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")

_MODS = {}


def norm(name):
    n = name if name.lower().endswith(".dll") else name + ".dll"
    if n.lower() in FORBIDDEN:
        raise SystemExit("refused: %s is never read (round-3 rules)" % n)
    return n


def get(name):
    n = norm(name)
    if n.lower() not in _MODS:
        _MODS[n.lower()] = Mod(n)
    return _MODS[n.lower()]


class Mod:
    def __init__(self, name):
        name = norm(name)
        self.m = Module(name)
        self.name = name
        self.short = name[:-4]
        self.slots = self.m.import_slots()
        self.exports = {r: n for n, o, r in self.m.exports()}
        self.export_by_name = {n: r for n, o, r in self.m.exports()}
        self.strs = self.m.strings()
        self._w = None
        self._pieces = None
        self._thunks = None
        self._vt = None
        self._md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self._md.detail = True
        self._icache = {}

    # ---- functions ---------------------------------------------------------

    def _build_pieces(self):
        m = self.m
        if m._funcs is None:
            m._load_funcs()
        parent = {b: p for b, e, p in m._funcs}

        def root(b):
            seen = set()
            while parent.get(b, b) != b and b not in seen:
                seen.add(b)
                b = parent[b]
            return b
        out = {}
        for b, e, p in m._funcs:
            out.setdefault(root(b), []).append((b, e))
        for k in out:
            out[k].sort()
        self._pieces = out
        self._root = root

    def func_of(self, rva):
        if self._pieces is None:
            self._build_pieces()
        e = self.m.entry_of(rva)
        return self._root(e[0]) if e else None

    def pieces(self, func):
        if self._pieces is None:
            self._build_pieces()
        return self._pieces.get(func, [])

    def size(self, func):
        return sum(e - b for b, e in self.pieces(func))

    def insns(self, func):
        """Instructions of every piece.  A function with no .pdata entry (a leaf) is decoded
        up to its first ret/jmp/int3, 200 instructions at most."""
        if func in self._icache:
            return self._icache[func]
        out = []
        ps = self.pieces(func)
        if ps:
            for b, e in ps:
                out.extend(self._md.disasm(self.m.bytes_at(b, e - b), b))
        else:
            far = func          # furthest forward branch target seen: decode at least to there
            for ins in self._md.disasm(self.m.bytes_at(func, 0x800), func):
                out.append(ins)
                if ins.mnemonic[0] == "j" and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                    t = ins.operands[0].imm
                    if func <= t < func + 0x800 and self.m.entry_of(t) is None:
                        far = max(far, t)
                if ins.mnemonic in ("ret", "int3", "jmp") and ins.address >= far:
                    break
                if len(out) >= 300:
                    break
        self._icache[func] = out
        return out

    def is_code(self, rva):
        s = self.m.pe.section_of(rva)
        return bool(s) and self.m.pe.is_exec(s)

    # ---- imports -----------------------------------------------------------

    @property
    def thunks(self):
        if self._thunks is None:
            self._thunks = {}
            for site, kind, slot in self.m.iat_refs(self.slots.keys()):
                if kind == "jmp" and self.m.entry_of(site) is None:
                    self._thunks[site] = self.slots[slot]
        return self._thunks

    def _follow_jmp(self, rva, depth=3):
        """rva of a `jmp rel32` stub's final target (incremental-link style), else rva."""
        while depth:
            b = self.m.bytes_at(rva, 5)
            if len(b) == 5 and b[0] == 0xE9 and self.m.entry_of(rva) is None:
                rva = rva + 5 + struct.unpack("<i", b[1:])[0]
                depth -= 1
            else:
                break
        return rva

    def call_target(self, ins):
        """(kind, value) for a call or jmp instruction:
             ("import", (dll, name))   through an import slot or its stub
             ("direct", rva)           a function of this module
             ("vslot", (reg, disp))    call [reg+disp]: a virtual or table call
             ("gptr", rva)             call [rip+x] on a global that is not an import slot
             ("reg", regname)          call reg
           None for anything else (jumps inside the function are the caller's business)."""
        op = ins.operands[0]
        if op.type == X.X86_OP_IMM:
            t = self._follow_jmp(op.imm)
            if t in self.thunks:
                return ("import", self.thunks[t])
            b = self.m.bytes_at(t, 6)
            if len(b) == 6 and b[0] == 0xFF and b[1] == 0x25:      # stub inside .pdata range
                slot = t + 6 + struct.unpack("<i", b[2:])[0]
                if slot in self.slots:
                    return ("import", self.slots[slot])
            return ("direct", t)
        if op.type == X.X86_OP_MEM:
            if op.mem.base == X.X86_REG_RIP:
                t = ins.address + ins.size + op.mem.disp
                if t in self.slots:
                    return ("import", self.slots[t])
                return ("gptr", t)
            if op.mem.base and not op.mem.index:
                return ("vslot", (ins.reg_name(op.mem.base), op.mem.disp))
            return ("vslot", (ins.op_str, 0))
        if op.type == X.X86_OP_REG:
            return ("reg", ins.reg_name(op.reg))
        return None

    # ---- strings and RTTI ----------------------------------------------------

    def wstrings(self):
        if self._w is None:
            self._w = {}
            for s in self.m.data_sections():
                buf = self.m.data[s["rptr"]:s["rptr"] + s["rsize"]]
                for mo in _RE_WIDE.finditer(buf):
                    if mo.start() % 2 == 0:
                        self._w[s["va"] + mo.start()] = mo.group().decode("utf-16le")
        return self._w

    def string_at(self, rva):
        if rva in self.strs:
            return self.strs[rva]
        w = self.wstrings().get(rva)
        return ("L:" + w) if w else None

    def cstr(self, rva, limit=200):
        """The NUL-terminated ASCII text at rva (also when rva points into a longer run)."""
        b = self.m.bytes_at(rva, limit)
        b = b.split(b"\0", 1)[0]
        if len(b) >= 1 and all(0x20 <= c < 0x7f or c in (9, 10, 13) for c in b):
            return b.decode("ascii")
        return None

    def vtable_class(self, rva):
        """Class name from MSVC x64 RTTI for a vtable whose first slot is at rva."""
        if self._vt is None:
            self._vt = {}
        if rva in self._vt:
            return self._vt[rva]
        name = None
        loc = self.m.u64(rva - 8)
        if loc and self.m.base < loc < self.m.base + 0x10000000:
            lo = loc - self.m.base
            hdr = self.m.bytes_at(lo, 24)
            if len(hdr) == 24:
                sig, off, cd, td, hd, selfrva = struct.unpack("<IIIIII", hdr)
                if sig == 1 and selfrva == lo:
                    t = self.cstr(td + 16, 160)
                    if t and t.startswith(".?A"):
                        name = t[4:].split("@@")[0]
                        parts = name.split("@")
                        name = "::".join(reversed([p for p in parts if p]))
                        if off:
                            name += "+0x%x" % off
        self._vt[rva] = name
        return name

    def vtables(self):
        """{vtable rva: class name} for every RTTI vtable of the module (cached)."""
        if getattr(self, "_allvt", None) is None:
            out = {}
            for s in self.m.data_sections():
                buf = self.m.data[s["rptr"]:s["rptr"] + s["rsize"]]
                n = len(buf) // 8
                lo, hi = self.m.base, self.m.base + 0x10000000
                for i, v in enumerate(struct.unpack_from("<%dQ" % n, buf, 0)):
                    if lo < v < hi and not self.is_code(v - lo):
                        c = self.vtable_class(s["va"] + 8 * i + 8)
                        if c:
                            out[s["va"] + 8 * i + 8] = c
            self._allvt = out
        return self._allvt

    def vtable_slots(self, rva, limit=600):
        """Code pointers of the table at rva, up to the first value that is not code."""
        out = []
        for i in range(limit):
            v = self.m.u64(rva + 8 * i)
            if v is None or not self.is_code(v - self.m.base):
                break
            if i and self.vtable_class(rva + 8 * i):
                break
            out.append(v - self.m.base)
        return out

    def label(self, rva):
        if rva in self.exports:
            return self.exports[rva]
        return "f%x" % rva

    # ---- whole-module cross-references (xref_lib cache) ------------------------

    @property
    def xr(self):
        """xref_lib's tables for this module: 'rip' (every rip-relative operand), 'calls'
        (direct call/jmp/jcc) and 'ind' (indirect calls).  Decoded once per module and cached
        in $SCOUT_CACHE (see xref_lib.py)."""
        if getattr(self, "_xr", None) is None:
            import xref_lib
            _, self._xr = xref_lib.load_cached(self.m.path)
        return self._xr

    def data_refs(self, rva):
        """[(site, mnemonic, writes_or_tests_memory_first)] for rip-relative uses of rva."""
        if getattr(self, "_ripidx", None) is None:
            idx = {}
            for addr, size, mn, tgt, fm in self.xr["rip"]:
                idx.setdefault(tgt, []).append((addr, mn, fm))
            self._ripidx = idx
        return self._ripidx.get(rva, [])

    def callers(self, rva):
        """[(site, 'call'|'jmp', caller function)] of direct calls to rva (thunks followed one
        step: a `jmp rva` stub's own callers are included)."""
        if getattr(self, "_cidx", None) is None:
            idx = {}
            for addr, mn, tgt in self.xr["calls"]:
                if mn in ("call", "jmp"):
                    idx.setdefault(tgt, []).append((addr, mn))
            self._cidx = idx
        out = []
        for site, mn in self._cidx.get(rva, []):
            f = self.func_of(site)
            if mn == "jmp" and f is not None and f == self.func_of(rva):
                continue
            if f is None:
                st = self.start_of(site)
                if mn == "jmp" and st == site:      # a one-jump stub: report who calls the stub
                    out.extend(self.callers(site))
                    continue
                f = st                              # a leaf with no unwind entry
            out.append((site, mn, f))
        return out

    def start_of(self, site):
        """Start of the function holding `site`: the .pdata function, or, for code with no
        unwind entry (a leaf), the nearest lower address that some call or lea refers to."""
        f = self.func_of(site)
        if f is not None:
            return f
        if getattr(self, "_targets", None) is None:
            import bisect
            t = set(tgt for addr, mn, tgt in self.xr["calls"] if mn in ("call", "jmp"))
            t |= set(tgt for addr, size, mn, tgt, fm in self.xr["rip"] if mn == "lea" and self.is_code(tgt))
            lo, hi = self.m.base, self.m.base + 0x10000000
            for sec in self.m.data_sections():      # code pointers in data: vtables, initializer tables
                buf = self.m.data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
                for v in struct.unpack_from("<%dQ" % (len(buf) // 8), buf, 0):
                    if lo < v < hi and self.is_code(v - lo):
                        t.add(v - lo)
            self._targets = sorted(t)
        import bisect
        i = bisect.bisect_right(self._targets, site) - 1
        while i >= 0:
            c = self._targets[i]
            if self.m.entry_of(c) is None and site - c < 0x400:
                return c
            if self.m.entry_of(c) is not None:
                break
            i -= 1
        return site

    def first_strings(self, func, n=3):
        out = []
        for ins in self.insns(func):
            if ins.mnemonic == "lea":
                for op in ins.operands:
                    if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                        s = self.string_at(ins.address + ins.size + op.mem.disp)
                        if s and s not in out:
                            out.append(s[:70])
            if len(out) >= n:
                break
        return out


_ORD = None


def ordinal_name(lib, name):
    """Name of an import by ordinal, from runtime/tools/imports.def (finding 24)."""
    global _ORD
    if _ORD is None:
        _ORD = {}
        p = os.path.join(HERE, "..", "..", "runtime", "tools", "imports.def")
        if os.path.exists(p):
            for line in open(p):
                f = line.split()
                if len(f) >= 3 and not line.startswith("#") and f[2] != "-":
                    _ORD[(f[0].lower(), "#" + f[2])] = f[1]
    return _ORD.get((lib.lower(), str(name)), str(name))


def fmt_imp(t):
    lib = t[0][:-4] if t[0].endswith(".dll") else t[0]
    n = str(t[1])
    if n.startswith("#"):
        nn = ordinal_name(t[0], n)
        n = "%s(%s)" % (nn, n) if nn != n else n
    return "%s!%s" % (lib, n)
