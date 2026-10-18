"""Image loading: a PE file (or a synthetic test image) mapped by RVA, relocatable to any base.

An Image holds the module as it would be mapped at its *preferred* base, indexed by RVA, plus
the facts the lifter and the harness need: executable ranges, DIR64 relocation sites, the
unwind table, import slots and exports.  `relocated(base)` returns a copy rebased to `base`
with every import slot filled with a distinct fake address (DESIGN.md sections 8 and 9).
"""

import hashlib
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "tools", "inventory"))
from pe_inventory import PE, UNW_FLAG_CHAININFO, REL_BASED_DIR64, REL_BASED_ABSOLUTE, SCN_MEM_WRITE  # noqa: E402

REPO = os.path.abspath(os.path.join(_HERE, ".."))


def walk_relocations(d, off, size):
    """The base relocation directory at d[off:off + size]: blocks of a page rva, a block size
    and 16-bit entries (type << 12 | offset in the page).  Returns (rvas of the DIR64 slots,
    sorted; problems).  A problem is a part of the directory that cannot be read as blocks: the
    entries behind it are lost, so the caller must not treat the list as complete.  The
    runtime's loader refuses such a file (runtime/core/loader_image.c, relocate); a type other
    than ABSOLUTE (padding) and DIR64 is refused here as it is there."""
    sites, problems = [], []
    end = off + size
    if off < 0 or end > len(d):
        return sites, ["the directory (0x%x bytes at file offset 0x%x) is outside the file" % (size, off)]
    while off < end:
        if end - off < 8:
            problems.append("%d bytes after the last block, too few for a block header" % (end - off))
            break
        page, block = struct.unpack_from("<II", d, off)
        if block < 8 or block % 2 or block > end - off:
            problems.append("the block for page 0x%x, 0x%x bytes before the end of the directory, has size 0x%x"
                            % (page, end - off, block))
            break
        for e in struct.unpack_from("<%dH" % ((block - 8) // 2), d, off + 8):
            typ, lo = e >> 12, e & 0xFFF
            if typ == REL_BASED_DIR64:
                sites.append(page + lo)
            elif typ != REL_BASED_ABSOLUTE:
                raise ValueError("unhandled relocation type %d at page 0x%x" % (typ, page))
        off += block
    return sorted(sites), problems


def find_module(module):
    """Map a module name ('tier0') to its DLL path under game/."""
    for d in ("game/bin/x64_retail", "game"):
        p = os.path.join(REPO, d, module + ".dll")
        if os.path.exists(p):
            return p
    raise FileNotFoundError("no DLL for module %r under game/" % module)


class Image:
    def __init__(self):
        self.name = ""
        self.image_base = 0          # preferred base
        self.size = 0                # size of image
        self.data = bytearray()      # mapped at the preferred base, indexed by RVA
        self.exec_ranges = []        # [(start_rva, end_rva)]
        self.writable_ranges = []    # [(start_rva, end_rva)] of writable, non-executable sections
        self.reloc_sites = []        # sorted RVAs of DIR64 slots
        self.pdata = []              # [(begin, end, unwind_rva)]
        self.import_slots = []       # [(slot_rva, "dll!name")], in table order
        self.exports = []            # [rva] that land in executable ranges
        self.entry_rva = 0
        self.header_size = 0         # SizeOfHeaders: the part of the image before the first section
        self.extra_entries = []      # synthetic images: explicit function entries
        self.sections = []           # [(name, start_rva, end_rva, end of the bytes that come from the file)]
        self.reloc_dir = (0, 0)      # (rva, size) of the base relocation directory
        self.reloc_problems = []     # what in that directory does not parse (walk_relocations)
        self.file_sha256 = ""        # SHA-256 of the file the image was read from; "" for a synthetic image

    # ---- construction ------------------------------------------------------

    @classmethod
    def from_pe(cls, path, name=None):
        pe = PE(path)
        if not pe.pe32plus or pe.machine != 0x8664:
            raise ValueError("%s is not an x86-64 PE32+ file" % path)
        im = cls()
        im.name = name or os.path.splitext(os.path.basename(path))[0].lower()
        im.name = "".join(c if c.isalnum() else "_" for c in im.name)
        im.image_base = pe.image_base
        im.size = pe.size_of_image
        im.entry_rva = pe.entry_rva
        d = pe.data
        im.file_sha256 = hashlib.sha256(d).hexdigest()
        opt = pe.e_lfanew + 24
        (hdr_size,) = struct.unpack_from("<I", d, opt + 60)
        im.header_size = hdr_size
        im.data = bytearray(im.size)
        im.data[:min(hdr_size, im.size)] = d[:min(hdr_size, im.size)]
        for s in pe.sections:
            n = min(s["rsize"], s["vsize"]) if s["vsize"] else s["rsize"]
            n = max(0, min(n, im.size - s["va"]))
            im.data[s["va"]:s["va"] + n] = d[s["rptr"]:s["rptr"] + n]
            im.sections.append((s["name"], s["va"], min(im.size, s["va"] + max(s["vsize"], 1)), s["va"] + n))
            if pe.is_exec(s):
                im.exec_ranges.append((s["va"], s["va"] + max(s["vsize"], 1)))
            elif s["flags"] & SCN_MEM_WRITE:
                im.writable_ranges.append((s["va"], min(im.size, s["va"] + max(s["vsize"], 1))))
        im.exec_ranges.sort()
        im.writable_ranges.sort()

        # relocations (same walk as PE.relocations, but keeping the sites and what does not parse)
        rva, size = pe.dirs[5]
        off = pe.rva_to_off(rva) if rva else None
        if off is not None:
            im.reloc_dir = (rva, size)
            im.reloc_sites, im.reloc_problems = walk_relocations(d, off, size)
        elif rva:
            im.reloc_problems = ["the directory at rva 0x%x is in no section" % rva]

        # unwind table
        rva, size = pe.dirs[3]
        if rva:
            for i in range(size // 12):
                b, e, u = struct.unpack_from("<III", im.data, rva + 12 * i)
                if e > b:
                    im.pdata.append((b, e, u))

        # import slots: FirstThunk array of every import descriptor, in order
        rva, size = pe.dirs[1]
        p = rva
        while rva and p + 20 <= im.size:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", im.data, p)
            if not (oft or name_rva or ft):
                break
            dll = (pe.cstr(name_rva) or "?").lower()
            names = pe._thunks(oft or ft)
            for i, n in enumerate(names):
                im.import_slots.append((ft + 8 * i, "%s!%s" % (dll, n)))
            p += 20

        # exports that are code
        rva, size = pe.dirs[0]
        if rva and rva + 40 <= im.size:
            nfunc, = struct.unpack_from("<I", im.data, rva + 20)
            funcs_rva, = struct.unpack_from("<I", im.data, rva + 28)
            for i in range(min(nfunc, 1 << 20)):
                (frva,) = struct.unpack_from("<I", im.data, funcs_rva + 4 * i)
                if frva and im.is_exec(frva):
                    im.exports.append(frva)
        return im

    @classmethod
    def synthetic(cls, name, size, chunks, exec_ranges, entries, import_slots=(), reloc_sites=(),
                  pdata=(), image_base=0x180000000, writable_ranges=()):
        """A minimal in-memory image for self-tests. `chunks` maps rva -> bytes."""
        im = cls()
        im.name = name
        im.image_base = image_base
        im.size = size
        im.data = bytearray(size)
        for rva, b in chunks.items():
            im.data[rva:rva + len(b)] = b
        im.exec_ranges = sorted(exec_ranges)
        im.writable_ranges = sorted(writable_ranges)
        im.extra_entries = list(entries)
        im.import_slots = [(rva, "fake!slot%d" % i) for i, rva in enumerate(import_slots)]
        im.reloc_sites = sorted(reloc_sites)
        im.pdata = list(pdata)
        return im

    # ---- queries -----------------------------------------------------------

    def is_exec(self, rva):
        for a, b in self.exec_ranges:
            if a <= rva < b:
                return True
        return False

    def section_of(self, rva):
        """(name, start, end, end of the bytes from the file) of the section holding rva, or
        None (the PE headers, a gap between sections).  An image without a section table (a
        synthetic one) is one section from the end of its headers to its end."""
        for sec in self.sections or [("image", self.header_size, self.size, self.size)]:
            if sec[1] <= rva < sec[2]:
                return sec
        return None

    def u32(self, rva):
        return struct.unpack_from("<I", self.data, rva)[0]

    def u64(self, rva):
        return struct.unpack_from("<Q", self.data, rva)[0]

    def code_pointer_targets(self):
        """RVAs of relocated 8-byte slots whose value points into an executable range."""
        out = set()
        for site in self.reloc_sites:
            if site + 8 <= self.size:
                t = self.u64(site) - self.image_base
                if 0 <= t < self.size and self.is_exec(t):
                    out.add(t)
        return out

    def pdata_functions(self):
        """Group unwind entries into functions.

        Returns ({root_entry_rva: [(begin, end), ...]}, n_fragments).  A chained entry
        (UNW_FLAG_CHAININFO) is a fragment of the function its chain leads to.
        """
        funcs = {}
        fragments = 0
        for b, e, u in self.pdata:
            root, chained = self._unwind_root(b, e, u)
            funcs.setdefault(root, []).append((b, e))
            if chained:
                fragments += 1
        for root in funcs:
            funcs[root] = sorted(set(funcs[root]))
        return funcs, fragments

    def _unwind_root(self, b, e, u):
        chained = False
        for _ in range(64):
            if u & 1:                       # indirect: points at another RUNTIME_FUNCTION
                p = u & ~1
                if p + 12 > self.size:
                    break
                b, e, u = struct.unpack_from("<III", self.data, p)
                chained = True
                continue
            if u + 4 > self.size:
                break
            flags = self.data[u] >> 3
            if not (flags & UNW_FLAG_CHAININFO):
                break
            ncodes = self.data[u + 2]
            p = u + 4 + 2 * ((ncodes + 1) & ~1)
            if p + 12 > self.size:
                break
            b, e, u = struct.unpack_from("<III", self.data, p)
            chained = True
        return b, chained

    # ---- mapping -----------------------------------------------------------

    def relocated(self, base, fake_import_base):
        """Bytes of the image as mapped at `base`: relocations applied, import slots faked."""
        out = bytearray(self.data)
        delta = base - self.image_base
        for site in self.reloc_sites:
            if site + 8 <= self.size:
                (v,) = struct.unpack_from("<Q", out, site)
                struct.pack_into("<Q", out, site, (v + delta) & 0xFFFFFFFFFFFFFFFF)
        for i, (slot, _) in enumerate(self.import_slots):
            struct.pack_into("<Q", out, slot, fake_import_base + 16 * i)
        return out
