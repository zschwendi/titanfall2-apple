#!/usr/bin/env python3
"""Phase 0 inventory of a Windows game's PE files.

Standard library only, so it runs on the Ubuntu machine without a venv.
Walks a directory for .exe/.dll, and for each file records what the
recompilation plan needs to know before Gate 0:

  - size of executable code, section layout and entropy
  - function ranges from the x64 unwind table (.pdata), and how many
    carry exception/unwind handlers or chained unwind info
  - base relocations, and how many relocated pointers land in code
    (function-pointer and vtable slots)
  - imports, delay imports and exports
  - RTTI class-name count, Rich header (compiler hint), PDB path
  - signs of packing or DRM wrapping

Writes inventory.json (everything) and INVENTORY.md (summary tables).

    python3 pe_inventory.py /path/to/Titanfall2 --out /path/to/out
"""

import argparse
import json
import math
import os
import struct
import sys
from collections import Counter, defaultdict

SCN_CNT_CODE = 0x00000020
SCN_MEM_EXECUTE = 0x20000000
SCN_MEM_WRITE = 0x80000000

UNW_FLAG_EHANDLER = 0x1
UNW_FLAG_UHANDLER = 0x2
UNW_FLAG_CHAININFO = 0x4

REL_BASED_ABSOLUTE = 0
REL_BASED_HIGHLOW = 3
REL_BASED_DIR64 = 10

MACHINES = {0x8664: "x86-64", 0x14C: "x86", 0xAA64: "arm64"}

# Section names and byte markers left by common packers and DRM wrappers.
PACKER_SECTIONS = {
    ".ooa": "EA/Origin DRM wrapper",
    ".bind": "Steam DRM stub",
    "UPX0": "UPX",
    "UPX1": "UPX",
    ".vmp0": "VMProtect",
    ".vmp1": "VMProtect",
    ".themida": "Themida",
    ".winlice": "WinLicense",
    ".arch": "Denuvo (commonly seen)",
    ".denuvo": "Denuvo",
    ".enigma1": "Enigma",
    ".aspack": "ASPack",
}

# MSVC toolset from the linker version in the optional header. The Rich header's build
# numbers are kept raw in the JSON but are not used for the hint: Visual Studio 2012 and
# 2013 build ranges interleave, so "highest build" picks the wrong one.
def toolset_hint(linker):
    major, minor = (int(x) for x in linker.split("."))
    if major == 14:
        if minor == 0:
            return "Visual Studio 2015 (14.0)"
        if minor < 20:
            return "Visual Studio 2017 (14.1x)"
        if minor < 30:
            return "Visual Studio 2019 (14.2x)"
        return "Visual Studio 2022 or later (14.%d)" % minor
    known = {6: "Visual C++ 6", 7: "Visual Studio 2002/2003", 8: "Visual Studio 2005",
             9: "Visual Studio 2008", 10: "Visual Studio 2010", 11: "Visual Studio 2012",
             12: "Visual Studio 2013"}
    return known.get(major, "linker %s (not MSVC, or unknown)" % linker)


def entropy(data):
    if not data:
        return 0.0
    counts = Counter(data)
    total = len(data)
    return -sum(c / total * math.log2(c / total) for c in counts.values())


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if len(d) < 0x40 or d[:2] != b"MZ":
            raise ValueError("not an MZ file")
        (self.e_lfanew,) = struct.unpack_from("<I", d, 0x3C)
        if d[self.e_lfanew:self.e_lfanew + 4] != b"PE\0\0":
            raise ValueError("no PE signature")
        coff = self.e_lfanew + 4
        (self.machine, nsec, self.timestamp, _, _, opt_size, self.characteristics) = struct.unpack_from(
            "<HHIIIHH", d, coff)
        opt = coff + 20
        (magic,) = struct.unpack_from("<H", d, opt)
        self.pe32plus = magic == 0x20B
        self.linker = "%d.%d" % struct.unpack_from("<BB", d, opt + 2)
        (self.entry_rva,) = struct.unpack_from("<I", d, opt + 16)
        if self.pe32plus:
            (self.image_base,) = struct.unpack_from("<Q", d, opt + 24)
            (self.dll_characteristics,) = struct.unpack_from("<H", d, opt + 70)
            (ndirs,) = struct.unpack_from("<I", d, opt + 108)
            dirs_off = opt + 112
        else:
            (self.image_base,) = struct.unpack_from("<I", d, opt + 28)
            (self.dll_characteristics,) = struct.unpack_from("<H", d, opt + 70)
            (ndirs,) = struct.unpack_from("<I", d, opt + 92)
            dirs_off = opt + 96
        (self.size_of_image,) = struct.unpack_from("<I", d, opt + 56)
        self.dirs = [struct.unpack_from("<II", d, dirs_off + 8 * i) for i in range(min(ndirs, 16))]
        self.dirs += [(0, 0)] * (16 - len(self.dirs))
        self.sections = []
        sec = opt + opt_size
        for i in range(nsec):
            name, vsize, va, rsize, rptr, _, _, _, _, ch = struct.unpack_from("<8sIIIIIIHHI", d, sec + 40 * i)
            self.sections.append({
                "name": name.rstrip(b"\0").decode("latin-1"),
                "vsize": vsize, "va": va, "rsize": rsize, "rptr": rptr, "flags": ch,
            })

    def rva_to_off(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]):
                delta = rva - s["va"]
                if delta < s["rsize"]:
                    return s["rptr"] + delta
                return None
        return None

    def cstr(self, rva, limit=512):
        off = self.rva_to_off(rva)
        if off is None:
            return None
        end = self.data.find(b"\0", off, off + limit)
        if end < 0:
            return None
        return self.data[off:end].decode("latin-1")

    def section_of(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]):
                return s
        return None

    def is_exec(self, s):
        return bool(s["flags"] & (SCN_MEM_EXECUTE | SCN_CNT_CODE))

    # ---- imports / exports -------------------------------------------------

    def _thunks(self, rva):
        names = []
        size = 8 if self.pe32plus else 4
        fmt = "<Q" if self.pe32plus else "<I"
        high = 1 << (63 if self.pe32plus else 31)
        off = self.rva_to_off(rva)
        while off is not None and off + size <= len(self.data):
            (v,) = struct.unpack_from(fmt, self.data, off)
            if v == 0:
                break
            if v & high:
                names.append("#%d" % (v & 0xFFFF))
            else:
                n = self.cstr((v & 0x7FFFFFFF) + 2)
                names.append(n if n is not None else "?")
            off += size
        return names

    def imports(self):
        out = {}
        rva, size = self.dirs[1]
        off = self.rva_to_off(rva) if rva else None
        while off is not None and off + 20 <= len(self.data):
            oft, _, _, name, ft = struct.unpack_from("<IIIII", self.data, off)
            if not (oft or name or ft):
                break
            dll = (self.cstr(name) or "?").lower()
            out.setdefault(dll, []).extend(self._thunks(oft or ft))
            off += 20
        return out

    def delay_imports(self):
        out = {}
        rva, size = self.dirs[13]
        off = self.rva_to_off(rva) if rva else None
        while off is not None and off + 32 <= len(self.data):
            attrs, name, _, _, int_rva, _, _, _ = struct.unpack_from("<IIIIIIII", self.data, off)
            if not (name or int_rva):
                break
            if attrs & 1:  # fields are RVAs (always the case for x64)
                dll = (self.cstr(name) or "?").lower()
                out.setdefault(dll, []).extend(self._thunks(int_rva))
            off += 32
        return out

    def exports(self):
        rva, size = self.dirs[0]
        off = self.rva_to_off(rva) if rva else None
        if off is None or off + 40 > len(self.data):
            return {"functions": 0, "names": []}
        _, _, _, _, _, _, nfunc, nnames, _, names_rva, _ = struct.unpack_from("<IIHHIIIIIII", self.data, off)
        names = []
        noff = self.rva_to_off(names_rva)
        if noff is not None:
            for i in range(min(nnames, 100000)):
                (nrva,) = struct.unpack_from("<I", self.data, noff + 4 * i)
                n = self.cstr(nrva)
                if n:
                    names.append(n)
        return {"functions": nfunc, "names": names}

    # ---- unwind table ------------------------------------------------------

    def runtime_functions(self):
        """x64 only. Returns stats on RUNTIME_FUNCTION entries and their unwind info."""
        rva, size = self.dirs[3]
        off = self.rva_to_off(rva) if rva else None
        if off is None or self.machine != 0x8664:
            return None
        n = size // 12
        stats = {"entries": n, "with_ehandler": 0, "with_uhandler": 0, "chained": 0,
                 "primary_ranges": 0, "bytes_covered": 0}
        handlers = Counter()
        for i in range(n):
            begin, end, unwind = struct.unpack_from("<III", self.data, off + 12 * i)
            stats["bytes_covered"] += max(0, end - begin)
            uoff = self.rva_to_off(unwind & ~1)
            if uoff is None or uoff + 4 > len(self.data):
                continue
            ver_flags, _, ncodes, _ = struct.unpack_from("<BBBB", self.data, uoff)
            flags = ver_flags >> 3
            if flags & UNW_FLAG_CHAININFO:
                stats["chained"] += 1
                continue
            stats["primary_ranges"] += 1
            if flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER):
                if flags & UNW_FLAG_EHANDLER:
                    stats["with_ehandler"] += 1
                if flags & UNW_FLAG_UHANDLER:
                    stats["with_uhandler"] += 1
                hoff = uoff + 4 + 2 * ((ncodes + 1) & ~1)
                if hoff + 4 <= len(self.data):
                    (h,) = struct.unpack_from("<I", self.data, hoff)
                    handlers[h] += 1
        stats["distinct_handlers"] = len(handlers)
        stats["top_handlers"] = [{"rva": "0x%x" % h, "functions": c} for h, c in handlers.most_common(8)]
        return stats

    # ---- relocations -------------------------------------------------------

    def relocations(self):
        rva, size = self.dirs[5]
        off = self.rva_to_off(rva) if rva else None
        stats = {"present": off is not None and size > 0, "dir64": 0, "highlow": 0,
                 "code_pointers": 0, "distinct_code_targets": 0, "code_pointers_in_exec_sections": 0}
        if not stats["present"]:
            return stats
        exec_ranges = [(s["va"], s["va"] + s["vsize"]) for s in self.sections if self.is_exec(s)]
        targets = set()
        end = off + size
        while off + 8 <= end and off + 8 <= len(self.data):
            page, block = struct.unpack_from("<II", self.data, off)
            if block < 8:
                break
            count = (block - 8) // 2
            entries = struct.unpack_from("<%dH" % count, self.data, off + 8)
            for e in entries:
                typ, lo = e >> 12, e & 0xFFF
                if typ == REL_BASED_DIR64:
                    stats["dir64"] += 1
                    site = page + lo
                    soff = self.rva_to_off(site)
                    if soff is not None and soff + 8 <= len(self.data):
                        (val,) = struct.unpack_from("<Q", self.data, soff)
                        t = val - self.image_base
                        if any(a <= t < b for a, b in exec_ranges):
                            stats["code_pointers"] += 1
                            targets.add(t)
                            if any(a <= site < b for a, b in exec_ranges):
                                stats["code_pointers_in_exec_sections"] += 1
                elif typ == REL_BASED_HIGHLOW:
                    stats["highlow"] += 1
            off += block
        stats["distinct_code_targets"] = len(targets)
        return stats

    # ---- compiler / debug hints -------------------------------------------

    def rich_header(self):
        d = self.data[:self.e_lfanew]
        pos = d.find(b"Rich")
        if pos < 0:
            return None
        (key,) = struct.unpack_from("<I", d, pos + 4)
        entries = []
        i = pos - 8
        while i >= 0x40:
            a, b = struct.unpack_from("<II", d, i)
            a ^= key
            b ^= key
            if a == 0x536E6144:  # "DanS"
                break
            entries.append({"product": a >> 16, "build": a & 0xFFFF, "count": b})
            i -= 8
        return {"entries": entries[::-1]}

    def pdb_path(self):
        rva, size = self.dirs[6]
        off = self.rva_to_off(rva) if rva else None
        if off is None:
            return None
        for i in range(size // 28):
            _, _, _, _, typ, dsize, _, ptr = struct.unpack_from("<IIHHIIII", self.data, off + 28 * i)
            if typ == 2 and self.data[ptr:ptr + 4] == b"RSDS":
                end = self.data.find(b"\0", ptr + 24, ptr + dsize + 1)
                return self.data[ptr + 24:end if end > 0 else ptr + dsize].decode("latin-1", "replace")
        return None

    def rtti(self):
        """MSVC RTTI type descriptors carry decorated names starting .?AV (class) or .?AU (struct)."""
        names = set()
        for tag in (b".?AV", b".?AU"):
            pos = self.data.find(tag)
            while pos >= 0:
                end = self.data.find(b"\0", pos, pos + 512)
                if end > 0:
                    names.add(self.data[pos:end].decode("latin-1"))
                pos = self.data.find(tag, pos + 4)
        return {"count": len(names), "sample": sorted(names)[:15]}

    # ---- packing heuristics -----------------------------------------------

    def packing_signs(self, sections_out, imports):
        signs = []
        for s in self.sections:
            if s["name"] in PACKER_SECTIONS:
                signs.append("section %s: %s" % (s["name"], PACKER_SECTIONS[s["name"]]))
        for s, so in zip(self.sections, sections_out):
            if self.is_exec(s):
                if so["entropy"] > 7.2:
                    signs.append("executable section %s has entropy %.2f (compressed or encrypted)"
                                 % (s["name"], so["entropy"]))
                if s["flags"] & SCN_MEM_WRITE:
                    signs.append("section %s is both writable and executable" % s["name"])
                if s["vsize"] > 0x1000 and s["rsize"] == 0:
                    signs.append("executable section %s has no bytes on disk (filled at run time)" % s["name"])
        es = self.section_of(self.entry_rva) if self.entry_rva else None
        if es is not None and es["name"] not in (".text", "CODE", ".code", "INIT"):
            signs.append("entry point is in section %s, not .text" % es["name"])
        n_imports = sum(len(v) for v in imports.values())
        code = sum(s["vsize"] for s in self.sections if self.is_exec(s))
        if code > 0x40000 and n_imports < 10:
            signs.append("only %d imports for %d KB of code" % (n_imports, code // 1024))
        return signs

    def summarise(self):
        sections_out = []
        for s in self.sections:
            raw = self.data[s["rptr"]:s["rptr"] + s["rsize"]] if s["rsize"] else b""
            sections_out.append({
                "name": s["name"], "vsize": s["vsize"], "rsize": s["rsize"],
                "exec": self.is_exec(s), "write": bool(s["flags"] & SCN_MEM_WRITE),
                "entropy": round(entropy(raw), 3),
            })
        imports = self.imports()
        delay = self.delay_imports()
        exports = self.exports()
        return {
            "file": os.path.basename(self.path),
            "size": len(self.data),
            "machine": MACHINES.get(self.machine, hex(self.machine)),
            "is_dll": bool(self.characteristics & 0x2000),
            "linker": self.linker,
            "toolset_hint": toolset_hint(self.linker),
            "timestamp": self.timestamp,
            "image_base": "0x%x" % self.image_base,
            "aslr": bool(self.dll_characteristics & 0x40),
            "signed": self.dirs[4][1] > 0,
            "has_tls": self.dirs[9][1] > 0,
            "is_dotnet": self.dirs[14][1] > 0,
            "code_bytes": sum(s["vsize"] for s in self.sections if self.is_exec(s)),
            "sections": sections_out,
            "imports": imports,
            "delay_imports": delay,
            "exports": {"functions": exports["functions"], "named": len(exports["names"]),
                        "sample": exports["names"][:20]},
            "unwind": self.runtime_functions(),
            "relocations": self.relocations(),
            "rich": self.rich_header(),
            "pdb_path": self.pdb_path(),
            "rtti": self.rtti(),
            "packing_signs": self.packing_signs(sections_out, imports),
        }


NEVER_READ = ("titanfall2.exe", "originsdk.dll")


def find_pe_files(root):
    for base, _, files in os.walk(root):
        for f in sorted(files):
            if f.lower() in NEVER_READ:
                continue        # the project's rule: never read, not even their headers
            if f.lower().endswith((".exe", ".dll")):
                yield os.path.join(base, f)


def mb(n):
    return "%.2f" % (n / 1048576)


def write_markdown(results, errors, root, path):
    local = {r["file"].lower() for r in results}
    lines = ["# PE inventory", "", "Root: `%s`" % root, "",
             "%d PE files, %s MB of executable code in total." % (
                 len(results), mb(sum(r["code_bytes"] for r in results))), ""]

    lines += ["## Files", "",
              "| File | Arch | Size MB | Code MB | Functions (.pdata) | With handlers | Relocs | Code pointers in data | "
              "Imports | Exports | RTTI names | Max exec entropy | Toolset hint |",
              "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in sorted(results, key=lambda r: -r["code_bytes"]):
        u = r["unwind"] or {}
        handlers = (u.get("with_ehandler", 0) + u.get("with_uhandler", 0)) if u else ""
        ent = max([s["entropy"] for s in r["sections"] if s["exec"]] or [0])
        n_imp = sum(len(v) for v in r["imports"].values()) + sum(len(v) for v in r["delay_imports"].values())
        lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %.2f | %s |" % (
            r["rel"], r["machine"], mb(r["size"]), mb(r["code_bytes"]),
            u.get("primary_ranges", ""), handlers, r["relocations"]["dir64"] + r["relocations"]["highlow"],
            r["relocations"]["code_pointers"], n_imp, r["exports"]["functions"], r["rtti"]["count"], ent,
            r["toolset_hint"]))

    lines += ["", "## Signs of packing or DRM wrapping", ""]
    flagged = [r for r in results if r["packing_signs"]]
    if not flagged:
        lines.append("None found by these heuristics.")
    for r in flagged:
        lines.append("- **%s**" % r["rel"])
        lines += ["  - %s" % s for s in r["packing_signs"]]

    lines += ["", "## PDB paths left in the files", ""]
    pdbs = [(r["rel"], r["pdb_path"]) for r in results if r["pdb_path"]]
    lines += ["- %s: `%s`" % p for p in pdbs] or ["None."]

    # Imports that leave the game: these are the runtime we have to write.
    ext = defaultdict(lambda: defaultdict(set))
    for r in results:
        for table in (r["imports"], r["delay_imports"]):
            for dll, funcs in table.items():
                if dll not in local:
                    for fn in funcs:
                        ext[dll][fn].add(r["file"])
    total = sum(len(f) for f in ext.values())
    lines += ["", "## External imports (the runtime surface)", "",
              "%d distinct functions across %d system libraries." % (total, len(ext)), "",
              "| Library | Distinct functions | Imported by |", "|---|---|---|"]
    for dll in sorted(ext, key=lambda d: -len(ext[d])):
        users = sorted({u for fs in ext[dll].values() for u in fs})
        lines.append("| %s | %d | %s |" % (dll, len(ext[dll]), ", ".join(users[:8]) + (" ..." if len(users) > 8 else "")))
    for dll in sorted(ext, key=lambda d: -len(ext[d])):
        lines += ["", "### %s" % dll, "", ", ".join("`%s`" % f for f in sorted(ext[dll]))]

    if errors:
        lines += ["", "## Files that could not be parsed", ""] + ["- %s: %s" % e for e in errors]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return total, len(ext)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="game install directory")
    ap.add_argument("--out", default=".", help="directory for inventory.json and INVENTORY.md")
    args = ap.parse_args()

    results, errors = [], []
    for path in find_pe_files(args.root):
        rel = os.path.relpath(path, args.root)
        try:
            r = PE(path).summarise()
            r["rel"] = rel
            results.append(r)
            print("ok   %s" % rel, file=sys.stderr)
        except Exception as e:  # keep going: one odd file must not hide the rest
            errors.append((rel, "%s: %s" % (type(e).__name__, e)))
            print("FAIL %s: %s" % (rel, e), file=sys.stderr)

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "inventory.json"), "w") as f:
        json.dump({"root": args.root, "files": results, "errors": errors}, f, indent=1)
    total, libs = write_markdown(results, errors, args.root, os.path.join(args.out, "INVENTORY.md"))
    print("%d files parsed, %d failed, %d external functions in %d libraries"
          % (len(results), len(errors), total, libs))


if __name__ == "__main__":
    main()
