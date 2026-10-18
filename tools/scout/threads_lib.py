#!/usr/bin/env python3
"""Shared helpers for the thread scouting scripts (issue #29).

Everything here is read-only on the game files. Standard library plus Capstone.

  Module(path)            parsed PE with import slots, function table, string index
  Module.func_of(rva)     enclosing function start (follows chained unwind info)
  Module.iat_refs(slots)  every code site that calls, jumps through or loads an import slot
  Module.string_refs()    code sites that load the address of a string (rip-relative lea)
  Module.abs_refs(rvas)   8-byte absolute pointers in data (relocated) to given RVAs
"""

import bisect
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GAME = os.path.join(ROOT, "game")

# Game-owned modules (everything the engine itself ships). Third-party SDKs are listed
# separately because we scan them for thread creation too but they are not Source code.
SOURCE_MODULES = ["tier0.dll", "vstdlib.dll", "launcher.dll", "engine.dll", "filesystem_stdio.dll",
                  "datacache.dll", "inputsystem.dll", "localize.dll", "materialsystem_dx11.dll",
                  "studiorender.dll", "vgui2.dll", "vguimatsurface.dll", "vphysics.dll",
                  "rtech_game.dll", "client.dll", "server.dll"]
THIRD_PARTY = ["mileswin64.dll", "binkawin64.dll", "bink2w64.dll", "auditionwin64.dll",
               "OriginSDK.dll", "amd_ags_x64.dll", "GFSDK_SSAO.win64.dll", "GFSDK_TXAA.win64.dll"]


def module_path(name):
    if name.lower() == "server.dll":
        return os.path.join(GAME, "server.dll")
    return os.path.join(GAME, "bin", "x64_retail", name)


def all_module_names():
    return SOURCE_MODULES + THIRD_PARTY


# Direct rip-relative references to an import slot.
#   ff 15 d32       call [rip+d32]   (optionally rex.w prefixed)
#   ff 25 d32       jmp  [rip+d32]
#   REX 8b modrm    mov r64, [rip+d32]    (modrm = 00 reg 101)
_RE_FF = re.compile(rb"\xff[\x15\x25]", re.S)
_RE_MOV = re.compile(rb"[\x48\x4c]\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", re.S)
# lea r64, [rip+d32]
_RE_LEA = re.compile(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", re.S)
_RE_REL = re.compile(rb"[\xe8\xe9]", re.S)
_RE_ASCII = re.compile(rb"[\x20-\x7e]{4,}")


class Module:
    def __init__(self, name):
        self.name = name
        self.path = module_path(name)
        self.pe = PE(self.path)
        self.base = self.pe.image_base
        self.data = self.pe.data
        self.secs = self.pe.sections
        self._funcs = None
        self._strings = None

    # ---- address helpers -------------------------------------------------

    def va(self, rva):
        return self.base + rva

    def sec_name(self, rva):
        s = self.pe.section_of(rva)
        return s["name"] if s else None

    def exec_sections(self):
        return [s for s in self.secs if self.pe.is_exec(s) and s["rsize"]]

    def data_sections(self):
        return [s for s in self.secs if s["rsize"] and not self.pe.is_exec(s)]

    def bytes_at(self, rva, n):
        off = self.pe.rva_to_off(rva)
        if off is None:
            return b""
        return self.data[off:off + n]

    def u64(self, rva):
        b = self.bytes_at(rva, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else None

    def u32(self, rva):
        b = self.bytes_at(rva, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None

    # ---- imports ---------------------------------------------------------

    def import_slots(self):
        """{slot_rva: (dll, name)} for normal imports."""
        out = {}
        pe = self.pe
        rva, _ = pe.dirs[1]
        off = pe.rva_to_off(rva) if rva else None
        while off is not None and off + 20 <= len(pe.data):
            oft, _, _, name, ft = struct.unpack_from("<IIIII", pe.data, off)
            if not (oft or name or ft):
                break
            dll = (pe.cstr(name) or "?").lower()
            thunks = pe._thunks(oft or ft)
            for i, n in enumerate(thunks):
                out[ft + 8 * i] = (dll, n)
            off += 20
        return out

    def exports(self):
        """[(name, ordinal, rva)] including forwarders skipped."""
        pe = self.pe
        rva, size = pe.dirs[0]
        off = pe.rva_to_off(rva) if rva else None
        if off is None:
            return []
        # export directory: Characteristics, TimeDateStamp, MajorVer, MinorVer, Name, Base,
        # NumberOfFunctions, NumberOfNames, AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals
        (_, _, _, _, _, ord_base, nfunc, nnames, addr_rva, names_rva, ords_rva) = struct.unpack_from(
            "<IIHHIIIIIII", pe.data, off)
        res = []
        noff = pe.rva_to_off(names_rva)
        ooff = pe.rva_to_off(ords_rva)
        aoff = pe.rva_to_off(addr_rva)
        for i in range(nnames):
            (nrva,) = struct.unpack_from("<I", pe.data, noff + 4 * i)
            (o,) = struct.unpack_from("<H", pe.data, ooff + 2 * i)
            (frva,) = struct.unpack_from("<I", pe.data, aoff + 4 * o)
            res.append((pe.cstr(nrva), o + ord_base, frva))
        return res

    # ---- function table --------------------------------------------------

    def _load_funcs(self):
        pe = self.pe
        rva, size = pe.dirs[3]
        off = pe.rva_to_off(rva) if rva else None
        entries = []
        if off is not None:
            for i in range(size // 12):
                begin, end, unwind = struct.unpack_from("<III", pe.data, off + 12 * i)
                parent = begin
                uoff = pe.rva_to_off(unwind & ~1)
                if uoff is not None:
                    ver_flags, _, ncodes, _ = struct.unpack_from("<BBBB", pe.data, uoff)
                    if (ver_flags >> 3) & 4:  # chained: parent RUNTIME_FUNCTION follows the codes
                        coff = uoff + 4 + 2 * ((ncodes + 1) & ~1)
                        (pb, _pe, _pu) = struct.unpack_from("<III", pe.data, coff)
                        parent = pb
                entries.append((begin, end, parent))
        entries.sort()
        self._funcs = entries
        self._fbegins = [e[0] for e in entries]

    def func_of(self, rva):
        if self._funcs is None:
            self._load_funcs()
        i = bisect.bisect_right(self._fbegins, rva) - 1
        if i < 0:
            return None
        b, e, parent = self._funcs[i]
        if b <= rva < e:
            return parent
        return None

    def entry_of(self, rva):
        """Raw .pdata entry (begin, end, parent) containing rva, fragments included."""
        if self._funcs is None:
            self._load_funcs()
        i = bisect.bisect_right(self._fbegins, rva) - 1
        if i >= 0 and self._funcs[i][0] <= rva < self._funcs[i][1]:
            return self._funcs[i]
        return None

    def func_end(self, begin):
        if self._funcs is None:
            self._load_funcs()
        i = bisect.bisect_left(self._fbegins, begin)
        if i < len(self._funcs) and self._funcs[i][0] == begin:
            return self._funcs[i][1]
        return None

    def n_funcs(self):
        if self._funcs is None:
            self._load_funcs()
        return len(self._funcs)

    # ---- scans -----------------------------------------------------------

    def iat_refs(self, slots):
        """Sites referencing any of the import slot RVAs. Returns [(site_rva, kind, slot_rva)]."""
        slots = set(slots)
        out = []
        for s in self.exec_sections():
            buf = self.data[s["rptr"]:s["rptr"] + s["rsize"]]
            va0 = s["va"]
            for m in _RE_FF.finditer(buf):
                p = m.start()
                if p + 6 > len(buf):
                    continue
                (d,) = struct.unpack_from("<i", buf, p + 2)
                tgt = va0 + p + 6 + d
                if tgt in slots:
                    kind = "call" if buf[p + 1] == 0x15 else "jmp"
                    out.append((va0 + p, kind, tgt))
            for m in _RE_MOV.finditer(buf):
                p = m.start()
                if p + 7 > len(buf):
                    continue
                (d,) = struct.unpack_from("<i", buf, p + 3)
                tgt = va0 + p + 7 + d
                if tgt in slots:
                    out.append((va0 + p, "load", tgt))
        out.sort()
        return out

    def rel_refs(self, targets):
        """Sites with e8/e9 rel32 whose target is in `targets` (import thunks are called this way).
        Returns [(site_rva, 'call'|'jmp', target_rva)]. Raw byte scan: a hit can in principle be a
        false positive inside another instruction, so callers verify the ones that matter."""
        targets = set(targets)
        out = []
        if not targets:
            return out
        for s in self.exec_sections():
            buf = self.data[s["rptr"]:s["rptr"] + s["rsize"]]
            va0 = s["va"]
            n = len(buf)
            for m in _RE_REL.finditer(buf):
                p = m.start()
                if p + 5 > n:
                    continue
                (d,) = struct.unpack_from("<i", buf, p + 1)
                tgt = va0 + p + 5 + d
                if tgt in targets:
                    out.append((va0 + p, "call" if buf[p] == 0xE8 else "jmp", tgt))
        return out

    def call_index(self):
        """{target_rva: [(site_rva, 'call'|'jmp')]} for all e8/e9 rel32 whose target is a function
        start in .pdata (raw byte scan, so a few false positives are possible). Cached per process."""
        if getattr(self, "_cidx", None) is not None:
            return self._cidx
        if self._funcs is None:
            self._load_funcs()
        starts = set(e[0] for e in self._funcs)
        idx = {}
        for s in self.exec_sections():
            buf = self.data[s["rptr"]:s["rptr"] + s["rsize"]]
            va0 = s["va"]
            n = len(buf)
            for m in _RE_REL.finditer(buf):
                p = m.start()
                if p + 5 > n:
                    continue
                (d,) = struct.unpack_from("<i", buf, p + 1)
                tgt = va0 + p + 5 + d
                if tgt in starts:
                    idx.setdefault(tgt, []).append((va0 + p, "call" if buf[p] == 0xE8 else "jmp"))
        self._cidx = idx
        return idx

    def lea_index(self):
        """{target_rva: [site_rva]} for rip-relative lea whose target is a function start."""
        if getattr(self, "_lidx", None) is not None:
            return self._lidx
        if self._funcs is None:
            self._load_funcs()
        starts = set(e[0] for e in self._funcs)
        idx = {}
        for site, tgt in self.lea_refs():
            if tgt in starts:
                idx.setdefault(tgt, []).append(site)
        self._lidx = idx
        return idx

    def lea_refs(self):
        """All rip-relative lea sites. Returns list of (site_rva, target_rva)."""
        out = []
        for s in self.exec_sections():
            buf = self.data[s["rptr"]:s["rptr"] + s["rsize"]]
            va0 = s["va"]
            for m in _RE_LEA.finditer(buf):
                p = m.start()
                if p + 7 > len(buf):
                    continue
                (d,) = struct.unpack_from("<i", buf, p + 3)
                out.append((va0 + p, va0 + p + 7 + d))
        return out

    def strings(self, min_len=4):
        """{rva: str} ASCII strings in non-executable sections (cached)."""
        if self._strings is not None:
            return self._strings
        res = {}
        for s in self.data_sections():
            buf = self.data[s["rptr"]:s["rptr"] + s["rsize"]]
            for m in _RE_ASCII.finditer(buf):
                if m.end() < len(buf) and buf[m.end()] != 0 and m.end() - m.start() >= 4:
                    # not NUL-terminated: likely part of binary data, still keep it
                    pass
                res[s["va"] + m.start()] = m.group().decode("ascii")
        self._strings = res
        return res

    def abs_refs(self, rvas):
        """Absolute 8-byte pointers (image base + rva) found in non-exec sections that point to given rvas.
        Returns {target_rva: [site_rva...]}."""
        want = {self.base + r: r for r in rvas}
        out = {}
        for s in self.data_sections():
            buf = self.data[s["rptr"]:s["rptr"] + s["rsize"]]
            n = len(buf) // 8
            vals = struct.unpack_from("<%dQ" % n, buf, 0)
            for i, v in enumerate(vals):
                if v in want:
                    out.setdefault(want[v], []).append(s["va"] + 8 * i)
        return out


def hexs(x):
    return "0x%x" % x


def fmt_va(m, rva):
    return "%s+0x%x" % (m.name, rva)
