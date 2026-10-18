#!/usr/bin/env python3
"""Shared helpers for the EA-SDK and anti-tamper scouts: cross-references in x64 PE files.

Decodes every function in a module's .pdata table (plus the gaps between them) with Capstone's
fast path and keeps only what the scouts need:

  rip    every instruction with a RIP-relative memory operand: (addr, size, mnemonic, target, first_op_is_mem)
  calls  every direct call/jmp/jcc: (addr, mnemonic, target)
  ind    indirect calls and jumps through a register or memory operand (addr, mnemonic, op_str)

Results are cached as pickles in a scratch directory so the analysis scripts can be re-run quickly.
Nothing here writes game bytes anywhere.
"""

import bisect
import os
import pickle
import re
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))
from pe_inventory import PE  # noqa: E402

import capstone  # noqa: E402

RIP_RE = re.compile(r"rip ([+-]) (0x[0-9a-fA-F]+)")
SCRATCH = os.environ.get("SCOUT_CACHE", os.path.join(tempfile.gettempdir(), "tf2-scout-xref-cache"))

UNW_CHAININFO = 4


def pdata(pe):
    """List of (begin, end, unwind_rva) from the exception directory."""
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return []
    return [struct.unpack_from("<III", pe.data, off + 12 * i) for i in range(size // 12)]


def parent_map(pe, entries):
    """Map each RUNTIME_FUNCTION begin to the begin of its primary (non-chained) function."""
    by_begin = {b: (b, e, u) for b, e, u in entries}
    out = {}
    for b, e, u in entries:
        cur = (b, e, u)
        for _ in range(8):
            uoff = pe.rva_to_off(cur[2] & ~1)
            if uoff is None:
                break
            ver_flags, _, ncodes, _ = struct.unpack_from("<BBBB", pe.data, uoff)
            if not (ver_flags >> 3) & UNW_CHAININFO:
                break
            poff = uoff + 4 + 2 * ((ncodes + 1) & ~1)
            pb, pe_, pu = struct.unpack_from("<III", pe.data, poff)
            cur = by_begin.get(pb, (pb, pe_, pu))
        out[b] = cur[0]
    return out


class Module:
    """One PE image with its function table and cached cross-references."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.pe = PE(path)
        self.base = self.pe.image_base
        self.entries = sorted(pdata(self.pe))
        self.begins = [b for b, _, _ in self.entries]
        self.parent = parent_map(self.pe, self.entries)
        self.exec_secs = [s for s in self.pe.sections if self.pe.is_exec(s)]

    # ---- address helpers -----------------------------------------------
    def func_of(self, rva):
        """Start RVA of the primary function containing rva, or None."""
        i = bisect.bisect_right(self.begins, rva) - 1
        if i < 0:
            return None
        b, e, _ = self.entries[i]
        if b <= rva < e:
            return self.parent.get(b, b)
        return None

    def unit_of(self, rva):
        """Like func_of, but code outside every .pdata range (unwind-less leaf functions and
        import/vtable thunks) is attributed to the start of its own 0xCC-delimited unit."""
        f = self.func_of(rva)
        if f is not None:
            return f
        off = self.pe.rva_to_off(rva)
        if off is None:
            return None
        data = self.pe.data
        i = off
        lim = max(off - 0x200, 0)
        while i > lim and data[i - 1] != 0xCC:
            i -= 1
        # a unit starts after int3 padding; map the file offset back to an RVA
        return rva - (off - i)

    def in_exec(self, rva):
        return any(s["va"] <= rva < s["va"] + s["vsize"] for s in self.exec_secs)

    def section_name(self, rva):
        s = self.pe.section_of(rva)
        return s["name"] if s else None

    def read(self, rva, n):
        off = self.pe.rva_to_off(rva)
        return None if off is None else self.pe.data[off:off + n]

    def u64(self, rva):
        b = self.read(rva, 8)
        return None if b is None or len(b) < 8 else struct.unpack("<Q", b)[0]

    def u32(self, rva):
        b = self.read(rva, 4)
        return None if b is None or len(b) < 4 else struct.unpack("<I", b)[0]

    def cstring(self, rva, limit=200):
        b = self.read(rva, limit)
        if not b:
            return None
        end = b.find(b"\0")
        if end <= 0:
            return None
        s = b[:end]
        if all(32 <= c < 127 or c in (9, 10, 13) for c in s) and len(s) >= 3:
            return s.decode("ascii")
        return None

    def wstring(self, rva, limit=200):
        b = self.read(rva, limit * 2)
        if not b or len(b) < 8:
            return None
        chars = []
        for i in range(0, len(b) - 1, 2):
            c = b[i] | (b[i + 1] << 8)
            if c == 0:
                break
            if not (32 <= c < 127):
                return None
            chars.append(chr(c))
        s = "".join(chars)
        return s if len(s) >= 3 else None

    def any_string(self, rva):
        return self.cstring(rva) or self.wstring(rva)

    # ---- imports with their IAT slot RVAs -------------------------------
    def iat_slots(self):
        """{slot_rva: (dll, name, 'import'|'delay')}"""
        pe = self.pe
        out = {}
        size = 8
        # regular imports
        rva, _ = pe.dirs[1]
        off = pe.rva_to_off(rva) if rva else None
        while off is not None:
            oft, _, _, name, ft = struct.unpack_from("<IIIII", pe.data, off)
            if not (oft or name or ft):
                break
            dll = (pe.cstr(name) or "?").lower()
            names = pe._thunks(oft or ft)
            for i, n in enumerate(names):
                out[ft + size * i] = (dll, n, "import")
            off += 20
        # delay imports
        rva, _ = pe.dirs[13]
        off = pe.rva_to_off(rva) if rva else None
        while off is not None:
            attrs, name, hmod, iat, int_rva, _, _, _ = struct.unpack_from("<IIIIIIII", pe.data, off)
            if not (name or int_rva):
                break
            dll = (pe.cstr(name) or "?").lower()
            names = pe._thunks(int_rva)
            for i, n in enumerate(names):
                out[iat + size * i] = (dll, n, "delay")
            off += 32
        return out

    # ---- decoding --------------------------------------------------------
    def segments(self):
        """Cover every executable section with (start, end) ranges: .pdata functions, then gaps."""
        segs = []
        for s in self.exec_secs:
            lo, hi = s["va"], s["va"] + min(s["vsize"], s["rsize"])
            cur = lo
            for b, e, _ in self.entries:
                if e <= lo or b >= hi:
                    continue
                b, e = max(b, lo), min(e, hi)
                if b > cur:
                    segs.append((cur, b))
                segs.append((b, e))
                cur = max(cur, e)
            if cur < hi:
                segs.append((cur, hi))
        return segs

    def decode(self):
        """Linear decode of all segments. Returns the cross-reference tables."""
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        rip, calls, ind = [], [], []
        ninsn = 0
        data = self.pe.data
        for start, end in self.segments():
            off = self.pe.rva_to_off(start)
            if off is None:
                continue
            buf = data[off:off + (end - start)]
            pos = 0
            while pos < len(buf):
                last = pos
                for addr, size, mn, ops in md.disasm_lite(buf[pos:], start + pos):
                    ninsn += 1
                    pos = addr - start + size
                    if "rip" in ops:
                        m = RIP_RE.search(ops)
                        if m:
                            d = int(m.group(2), 16)
                            tgt = addr + size + (d if m.group(1) == "+" else -d)
                            first_mem = ops.startswith("[") or ("ptr [" in ops.split(",")[0])
                            rip.append((addr, size, mn, tgt, first_mem))
                    if mn == "call" or mn == "jmp" or mn[0] == "j":
                        if ops.startswith("0x"):
                            calls.append((addr, mn, int(ops, 16)))
                        elif mn in ("call", "jmp"):
                            ind.append((addr, mn, ops))
                if pos < len(buf):
                    pos += 1  # decoder stopped on an undecodable byte: skip it and resume
        return {"rip": rip, "calls": calls, "ind": ind, "ninsn": ninsn}


def load_cached(path):
    """Decode (or load from cache) a module. Returns (Module, tables)."""
    os.makedirs(SCRATCH, exist_ok=True)
    mod = Module(path)
    cache = os.path.join(SCRATCH, mod.name + ".%d.pkl" % os.path.getsize(path))
    if os.path.exists(cache):
        with open(cache, "rb") as f:
            return mod, pickle.load(f)
    tables = mod.decode()
    with open(cache, "wb") as f:
        pickle.dump(tables, f)
    return mod, tables


# ---- RTTI: map function RVAs to the classes whose vtables list them -----------------------

def vtables(mod):
    """Return {vtable_rva: class_name} for every vtable found through RTTI complete-object locators."""
    pe = mod.pe
    base = mod.base
    out = {}
    # A COL is {sig=1, offset, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf} (x64: all RVAs)
    # Find vtables by scanning relocated qwords in data sections whose value points at a COL.
    cols = {}
    for s in pe.sections:
        if pe.is_exec(s) or s["rsize"] == 0:
            continue
        raw = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
        for i in range(0, len(raw) - 24 + 1, 4):
            sig, off1, cdoff, td, cd, selfrva = struct.unpack_from("<IIIIII", raw, i)
            if sig == 1 and selfrva == s["va"] + i and 0 < td < pe.size_of_image and 0 < cd < pe.size_of_image:
                cols[selfrva] = td
    for s in pe.sections:
        if pe.is_exec(s) or s["rsize"] == 0:
            continue
        raw = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
        for i in range(0, len(raw) - 8 + 1, 8):
            (v,) = struct.unpack_from("<Q", raw, i)
            r = v - base
            if r in cols:
                td = cols[r]
                nm = pe.cstr(td + 16)
                out[s["va"] + i + 8] = nm
    return out


def vtable_slots(mod, vts):
    """Return {function_rva: [(class, slot), ...]} for functions listed in vtables."""
    pe = mod.pe
    base = mod.base
    res = {}
    for vt, nm in vts.items():
        slot = 0
        while slot < 600:
            v = mod.u64(vt + 8 * slot)
            if v is None:
                break
            r = v - base
            if not mod.in_exec(r):
                break
            res.setdefault(r, []).append((nm, slot))
            slot += 1
    return res


def demangle_class(nm):
    """'.?AVCFoo@@' -> 'CFoo'; '.?AVBar@Ns@@' -> 'Ns::Bar'."""
    if not nm:
        return nm
    m = re.match(r"\.\?A[VU](.*)@@$", nm)
    if not m:
        return nm
    parts = m.group(1).split("@")
    return "::".join(reversed(parts))


# ---- exports, relocations, call graph ------------------------------------------------------

def export_rvas(pe):
    """{name: rva} for the named exports (forwarders are skipped)."""
    rva, size = pe.dirs[0]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return {}
    _, _, _, _, _, _, nfunc, nnames, addr_rva, names_rva, ord_rva = struct.unpack_from("<IIHHIIIIIII", pe.data, off)
    out = {}
    noff, ooff, aoff = pe.rva_to_off(names_rva), pe.rva_to_off(ord_rva), pe.rva_to_off(addr_rva)
    for i in range(nnames):
        (nrva,) = struct.unpack_from("<I", pe.data, noff + 4 * i)
        (idx,) = struct.unpack_from("<H", pe.data, ooff + 2 * i)
        (frva,) = struct.unpack_from("<I", pe.data, aoff + 4 * idx)
        if rva <= frva < rva + size:
            continue  # forwarder
        out[pe.cstr(nrva)] = frva
    return out


def data_pointers_to(mod, targets_rva):
    """Find 8-byte-aligned qwords in non-executable sections equal to base+rva for rva in targets.
    Returns {target_rva: [site_rva, ...]}."""
    pe = mod.pe
    want = {mod.base + t: t for t in targets_rva}
    res = {}
    for s in pe.sections:
        if pe.is_exec(s) or s["rsize"] == 0:
            continue
        raw = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
        n = len(raw) // 8
        vals = struct.unpack_from("<%dQ" % n, raw, 0)
        for i, v in enumerate(vals):
            t = want.get(v)
            if t is not None:
                res.setdefault(t, []).append(s["va"] + 8 * i)
    return res


class Graph:
    """Direct-call graph of one module, keyed by raw target address so import thunks (which have
    no .pdata entry) stay distinct from the functions around them."""

    def __init__(self, mod, tables):
        self.mod = mod
        self.callers = {}   # callee address (RVA) -> [(site, 'call'|'jmp', caller_func)]
        self.callees = {}   # caller function start -> {callee address}
        self.taken = {}     # code address -> [(site, caller_func)]   (address loaded with lea)
        for addr, mn, tgt in tables["calls"]:
            if mn not in ("call", "jmp"):
                continue
            cf = mod.unit_of(addr)
            if mn == "jmp" and mod.unit_of(tgt) == cf and cf is not None:
                continue  # control flow inside one function, not a call
            self.callers.setdefault(tgt, []).append((addr, mn, cf))
            if cf is not None:
                self.callees.setdefault(cf, set()).add(tgt)
        for addr, size, mn, tgt, fm in tables["rip"]:
            if mn == "lea" and mod.in_exec(tgt):
                self.taken.setdefault(tgt, []).append((addr, mod.unit_of(addr)))
