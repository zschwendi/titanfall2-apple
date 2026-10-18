"""Relocated fields, and values that look like one: the relocation census and lint
(docs/DESIGN.md section 1; the triage behind the kinds is docs/handoff/lifter-reloc-50.md).

A relocated field is an 8-byte slot to which the Windows loader adds the load address minus
the preferred base (a DIR64 entry of the base relocation directory).  The oracle and the
lifted code share one list of them, so a field that is missing from the list, or that lies
where the lifter does not look, is wrong on both sides and no differential test shows it.
Two questions are therefore answered from the bytes of the module and from the instructions
the lifter decoded, and every answer is put into a *kind*:

  A. Where does each relocated field lie?  (SITE_KINDS)
  B. Which 8-byte values, at any byte offset, lie inside the image as mapped at its
     preferred base and are not a relocated field?  (VALUE_KINDS)  Plus the same question
     about the constants of the generated C, where every relocated address is written
     BASE + rva, so an absolute one that survives has no relocation behind it.

A kind is either known - it says what the bytes are and why the lifter and the loader are
right about them - or unknown: the shape of a missed or misplaced relocation, which a person
has to look at.  The lift of a module with anything of an unknown kind exits non-zero.

The rules use only structure that can be read off the module, never a list of addresses:

  * x86-64 has two 8-byte address fields, the immediate of 'mov r64, imm64' and the absolute
    address of 'mov' to and from the accumulator (moffs64).  So 8 bytes that overlap a decoded
    instruction without being exactly one of its 64-bit fields are parts of instructions, not
    an address, whatever value they spell.
  * A compiler aligns a pointer in data to 8 bytes unless a structure is packed.  Whether a
    module has packed pointers is read from the module itself: if every one of its relocated
    fields outside code is 8-aligned, an unaligned value is two neighbouring fields.  If one
    is not, the argument is gone and unaligned values are of an unknown kind.
  * Nothing addresses the PE headers past the image base (code that needs a header field
    computes it from the base at run time), so a value that would point there is not a
    pointer.  A value equal to the base itself gets no such excuse.
"""

import bisect
import re
import struct

# kind -> what it is and what handles it.  Insertion order is the order of the report.
SITE_KINDS = {
    "data": "8-aligned, in a section that is not executable: the loader adds the delta and lifted code reads "
            "the slot at run time",
    "data-unaligned": "the same at an address that is not a multiple of 8 (a packed structure)",
    "code-imm64": "the 64-bit immediate of a decoded instruction: translated as BASE + rva",
    "code-moffs64": "the 64-bit absolute address of a decoded instruction: translated as BASE + rva",
    "code-partial": "overlaps a decoded instruction without being one of its 64-bit fields (data decoded as code): "
                    "every function that decodes it is skipped with a reason of kind 'reloc'",
    "code-not-decoded": "in an executable section, in bytes that no function decodes (a table of pointers placed "
                        "in a code section): relocated by the loader like any data",
}
SITE_UNKNOWN = {
    "site-overlap": "begins less than 8 bytes after the previous relocated field",
    "site-in-headers": "inside the PE headers",
    "site-outside-sections": "in no section",
    "site-crosses-section-end": "its 8 bytes run past the end of the section",
    "site-in-relocation-directory": "inside the relocation directory itself",
    "target-outside-image": "the value does not point into the image at its preferred base",
}
VALUE_KINDS = {
    "instruction-bytes": "overlaps a decoded instruction and is not one of its 64-bit fields: parts of "
                         "instructions (a 32-bit displacement or immediate next to zero bytes)",
    "header-offset": "would point into the PE headers past the image base, where nothing is addressed",
    "unaligned": "not at a multiple of 8 and outside decoded code, in a module whose every relocated field outside "
                 "code is 8-aligned: two neighbouring fields, or bytes of code that no function reaches",
}
VALUE_UNKNOWN = {
    "field64-without-relocation": "a whole 64-bit immediate or absolute address of a decoded instruction",
    "aligned-pointer": "8-aligned and outside decoded code: the shape of a pointer whose relocation is missing",
    "unaligned-pointer": "not 8-aligned, in a module that has relocated fields which are not either",
}
OTHER_UNKNOWN = {
    "constant-in-c": "a constant of the generated C that lies inside the image at its preferred base",
    "directory": "a part of the relocation directory that cannot be read as blocks: entries may be lost",
}
UNKNOWN = dict(SITE_UNKNOWN, **VALUE_UNKNOWN, **OTHER_UNKNOWN)
LISTED = 4096          # at most this many rvas are kept per kind in the report


def scan_values(image):
    """[(rva, value - preferred base)] of every 8-byte window past the PE headers whose value
    lies inside the image as mapped at its preferred base and that is not a relocated field."""
    lo, hi = image.image_base, image.image_base + image.size
    sites = set(image.reloc_sites)
    data = bytes(image.data)
    out = []
    for top in sorted({lo >> 32, (hi - 1) >> 32}):
        pat = struct.pack("<I", top)
        pos = data.find(pat, 4)
        while pos >= 0:
            site = pos - 4
            (v,) = struct.unpack_from("<Q", data, site)
            if lo <= v < hi and site not in sites and site >= image.header_size:
                out.append((site, v - lo))
            pos = data.find(pat, pos + 1)
    return sorted(out)


class RelocLint:
    """Feed it every function the lifter decodes (see), then ask for the result."""

    def __init__(self, image, prog=None):
        self.image = image
        self.prog = prog            # the discovery (Program), to tell a function entry from other code
        self.values = scan_values(image)
        # 8-byte windows in executable sections whose covering instructions are wanted
        self.watch = sorted({s for s in image.reloc_sites if self._touches_code(s)}
                            | {s for s, _ in self.values if self._touches_code(s)})
        self.cover = {s: {} for s in self.watch}        # window -> {instruction rva: its 64-bit field there, or None}

    def _touches_code(self, rva):
        return self.image.is_exec(rva) or self.image.is_exec(min(rva + 7, self.image.size - 1))

    def see(self, prog, addrs):
        """addrs: sorted rvas of the instructions of one function, lifted or skipped."""
        if not self.watch or not addrs:
            return
        k = bisect.bisect_left(self.watch, addrs[0] - 7)
        end = bisect.bisect_right(self.watch, addrs[-1] + 14)
        for w in self.watch[k:end]:
            j = bisect.bisect_left(addrs, w - 14)
            while j < len(addrs) and addrs[j] < w + 8:
                a = addrs[j]
                j += 1
                if a in self.cover[w]:
                    continue
                ins = prog.decode(a)
                if ins is None or a + ins.size <= w:
                    continue
                field = None
                if ins.imm_size == 8 and a + ins.imm_offset == w:
                    field = "imm64"
                elif ins.disp_size == 8 and a + ins.disp_offset == w:
                    field = "moffs64"
                self.cover[w][a] = field

    # ---- A: the relocated fields -------------------------------------------------

    def _site_kind(self, site, prev):
        im = self.image
        if prev is not None and site - prev < 8:
            return "site-overlap"
        sec = im.section_of(site)
        if sec is None:
            return "site-in-headers" if site < im.header_size else "site-outside-sections"
        if site + 8 > sec[2]:
            return "site-crosses-section-end"
        d, n = im.reloc_dir
        if n and d - 8 < site < d + n:
            return "site-in-relocation-directory"
        if not (0 <= im.u64(site) - im.image_base < im.size):
            return "target-outside-image"
        cover = self.cover.get(site)
        if cover:
            fields = set(cover.values())
            return "code-partial" if None in fields or len(fields) > 1 else "code-" + fields.pop()
        if self._touches_code(site):
            return "code-not-decoded"
        return "data" if site % 8 == 0 else "data-unaligned"

    def _target(self, site):
        im = self.image
        if not (0 <= site <= im.size - 8):
            return "nothing (the field is not inside the image)"
        t = im.u64(site) - im.image_base
        if not (0 <= t < im.size):
            return "outside the image"
        if t == 0:
            return "the image base"
        if im.section_of(t) is None:
            return "the PE headers" if t < im.header_size else "a gap between sections"
        if not im.is_exec(t):
            return "data"
        # Discovery makes an entry of every relocated pointer to code outside the unwind-described
        # functions; one into the middle of such a function stays a label (a slot of a table of
        # absolute jump targets), and a call through it would stop at run time.
        if self.prog is not None and t not in self.prog.funcs:
            return "code inside a function (not an entry)"
        return "code"

    # ---- B: values that look like a relocated field ------------------------------

    def _value_kind(self, site, target, packed):
        cover = self.cover.get(site)
        if cover:
            return "field64-without-relocation" if any(cover.values()) else "instruction-bytes"
        if 0 < target < self.image.header_size:
            return "header-offset"
        if site % 8:
            return "unaligned-pointer" if packed else "unaligned"
        return "aligned-pointer"

    def result(self, texts):
        """texts: [(function name, its C)] of the lifted functions, or None if no C was made."""
        im = self.image
        sites, targets, where = {}, {}, {}
        prev = None
        for site in im.reloc_sites:
            kind = self._site_kind(site, prev)
            prev = site
            sites[kind] = sites.get(kind, 0) + 1
            where.setdefault(kind, []).append(site)
            t = self._target(site)
            targets[t] = targets.get(t, 0) + 1
        # a relocated field outside decoded code that is not 8-aligned: this module packs pointers
        packed = sites.get("data-unaligned", 0) + sum(1 for s in where.get("code-not-decoded", ()) if s % 8)
        values = {}
        for site, target in self.values:
            kind = self._value_kind(site, target, packed)
            e = values.setdefault(kind, {"count": 0, "sections": {}, "rvas": []})
            e["count"] += 1
            sec = im.section_of(site)
            name = sec[0] if sec else "(no section)"
            e["sections"][name] = e["sections"].get(name, 0) + 1
            if len(e["rvas"]) < LISTED:
                e["rvas"].append("0x%x" % site)
        in_c = None
        if texts is not None:
            lo, hi = im.image_base, im.image_base + im.size
            in_c = []
            for name, text in texts:
                code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
                for m in re.finditer(r"0x[0-9a-fA-F]+", code):
                    if lo <= int(m.group(0), 16) < hi:
                        in_c.append([name, m.group(0)])
        unknown = {k: n for k, n in sites.items() if k in SITE_UNKNOWN}
        unknown.update({k: e["count"] for k, e in values.items() if k in VALUE_UNKNOWN})
        if in_c:
            unknown["constant-in-c"] = len(in_c)
        if im.reloc_problems:
            unknown["directory"] = len(im.reloc_problems)
        order = list(SITE_KINDS) + list(SITE_UNKNOWN)
        vorder = list(VALUE_KINDS) + list(VALUE_UNKNOWN)
        into_headers = sum(1 for _, target in self.values if 0 < target < im.header_size)
        return {
            "relocated_fields": len(im.reloc_sites),
            "fields_by_kind": {k: sites[k] for k in order if k in sites},
            "fields_point_at": dict(sorted(targets.items())),
            "fields_not_8_aligned_outside_code": packed,
            "fields_of_unknown_kind": {k: ["0x%x" % s for s in where[k][:LISTED]] for k in SITE_UNKNOWN if k in where},
            "directory_problems": list(im.reloc_problems),
            "values_without_relocation": len(self.values),
            "values_by_kind": {k: values[k] for k in vorder if k in values},
            "constants_in_c": None if in_c is None else in_c[:32],
            "unknown": unknown,
            # the three counts the lint reported before it knew kinds (fix-pass handoff, item 8)
            "unrelocated_constants_in_c": None if in_c is None else len(in_c),
            "unrelocated_pointers_in_image": len(self.values) - into_headers,
            "values_pointing_into_the_headers": into_headers,
        }


def describe(lint):
    """The lines of the summary: one for the fields, one for the values, one per unknown kind."""
    out = ["relocated fields: %d%s; they point at: %s" % (
        lint["relocated_fields"],
        "".join(", %d %s" % (n, k) for k, n in lint["fields_by_kind"].items()),
        ", ".join("%s %d" % kv for kv in lint["fields_point_at"].items()) or "nothing")]
    c = lint["unrelocated_constants_in_c"]
    out.append("relocation lint: %s; %d 8-byte values in the image lie inside it at its preferred base and have "
               "no relocation%s" % (
                   "no C was generated, so its constants were not checked" if c is None
                   else "%d preferred-base constants in the generated C" % c,
                   lint["values_without_relocation"],
                   "".join("%s %d %s (%s)" % (":" if i == 0 else ",", e["count"], k,
                                              ", ".join("%s %d" % kv for kv in e["sections"].items()))
                           for i, (k, e) in enumerate(lint["values_by_kind"].items()))))
    for k, n in lint["unknown"].items():
        if k in SITE_UNKNOWN:
            ex = lint["fields_of_unknown_kind"][k][:6]
        elif k in VALUE_UNKNOWN:
            ex = lint["values_by_kind"][k]["rvas"][:6]
        elif k == "constant-in-c":
            ex = lint["constants_in_c"][:3]
        else:
            ex = lint["directory_problems"][:3]
        out.append("UNKNOWN KIND %s: %d  <-- LOOK: %s: %s" % (k, n, UNKNOWN[k], ex))
    return out
