#!/usr/bin/env python3
"""Relocations of a game module, looked at one by one (issue 50; docs/handoff/lifter-reloc-50.md).

    .venv/bin/python tools/scout/reloc_triage.py engine            # counts
    .venv/bin/python tools/scout/reloc_triage.py engine --list     # and every value without a relocation
    .venv/bin/python tools/scout/reloc_triage.py --dll tests/corpus/build/corpus_O2.dll

What the lift's relocation lint (lifter/relocs.py) reports by kind, with the detail a person
needs to check a kind: the relocation directory block by block, where the relocated fields
lie (section, alignment, what they point at), and for every 8-byte value that lies inside the
image at its preferred base and has no relocation: its section, its address modulo 8, where
it would point, the instructions the lifter decoded over it and which of their fields make up
its bytes.

Runs the lifter's discovery and decodes every function (about as long as a lift without the
translation); single process; writes nothing.  Prints addresses, mnemonics and counts only.
"""

import argparse
import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))

from lifter.discover import Program                    # noqa: E402
from lifter.image import Image, PE, find_module        # noqa: E402
from lifter.relocs import RelocLint                    # noqa: E402


def directory(pe, image):
    """Blocks, entries by type, bytes that are not blocks, and what follows the directory in
    its section."""
    rva, size = pe.dirs[5]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return "no relocation directory"
    d, end, blocks, types, pages = pe.data, off + size, 0, Counter(), []
    while off + 8 <= end:
        page, block = struct.unpack_from("<II", d, off)
        if block < 8 or block % 2 or off + block > end:
            break
        types.update(e >> 12 for e in struct.unpack_from("<%dH" % ((block - 8) // 2), d, off + 8))
        pages.append(page)
        blocks += 1
        off += block
    sec = image.section_of(rva)
    tail = bytes(image.data[rva + size:sec[2]]) if sec else b""
    return ("directory: 0x%x bytes at rva 0x%x in %s: %d blocks, pages ascending and distinct: %s, entries by type %s, "
            "%d bytes not read as blocks; after it the section has %d more bytes, %d of them not zero" % (
                size, rva, sec[0] if sec else "no section", blocks,
                pages == sorted(set(pages)), dict(sorted(types.items())), end - off, len(tail), sum(1 for b in tail if b)))


def fields(image, prog):
    c = Counter()
    prev = None
    starts = {b for b, _, _ in image.pdata}
    for site in image.reloc_sites:
        sec = image.section_of(site)
        c["in %s" % (sec[0] if sec else "no section")] += 1
        if sec and site + 8 > sec[3]:
            c["beyond the section's bytes in the file"] += 1
        if site % 8:
            c["not 8-aligned"] += 1
        if prev is not None and site - prev < 8:
            c["overlapping the previous one"] += 1
        prev = site
        t = image.u64(site) - image.image_base
        if not (0 <= t < image.size):
            c["-> outside the image"] += 1
        elif image.is_exec(t):
            root = prog.container(t)
            c["-> code: " + ("the start of an unwind entry" if t in starts else
                             "inside an unwind-described function" if root is not None else
                             "a function with no unwind entry" if t in prog.funcs else "not a function")] += 1
        else:
            tsec = image.section_of(t)
            c["-> %s" % (tsec[0] if tsec else "the image base" if t == 0 else "the PE headers")] += 1
    return c


def pattern(image, prog, site, cover):
    """Which instruction fields the 8 bytes at `site` are made of: (the shape, the shape with
    its values)."""
    if not cover:
        return ("no decoded instruction",) * 2
    for a in sorted(cover):
        ins = prog.decode(a)
        for kind, n, at in (("immediate", ins.imm_size, a + ins.imm_offset), ("displacement", ins.disp_size, a + ins.disp_offset)):
            if n == 4 and at == site + 4 and image.u32(at) == 1:
                shape = "one byte, then a 32-bit %s of 1" % kind
                return shape, "%s (the byte is 0x%02x)" % (shape, image.data[site + 3])
    for a in sorted(cover):
        ins = prog.decode(a)
        if ins.disp_size == 4 and a + ins.disp_offset == site + 3:
            shape = "three bytes, then a 32-bit displacement, then a zero byte"
            return shape, "%s (the displacement is 0x%x)" % (shape, image.u32(site + 3))
    return ("other parts of instructions",) * 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("modules", nargs="*", help="module names, looked up under game/")
    ap.add_argument("--dll", help="explicit path to one DLL instead")
    ap.add_argument("--list", action="store_true", help="one line per value without a relocation")
    args = ap.parse_args()
    paths = [args.dll] if args.dll else [find_module(m) for m in args.modules]
    if not paths:
        ap.error("give module names or --dll")
    for path in paths:
        image = Image.from_pe(path)
        prog = Program(image)
        prog.discover()
        lint = RelocLint(image, prog)
        for entry in sorted(prog.funcs):
            lint.see(prog, sorted(prog.build_cfg(prog.funcs[entry]).insns))
        res = lint.result(None)
        print("%s: image 0x%x bytes at 0x%x, %d functions" % (image.name, image.size, image.image_base, len(prog.funcs)))
        print("  " + directory(PE(path), image))
        print("  relocated fields: %d; by kind %s" % (res["relocated_fields"], res["fields_by_kind"]))
        print("    " + "; ".join("%s %d" % kv for kv in sorted(fields(image, prog).items())))
        print("  values without a relocation: %d; by kind %s; unknown %s" % (
            res["values_without_relocation"], {k: e["count"] for k, e in res["values_by_kind"].items()}, res["unknown"]))
        kind_of = {int(r, 16): k for k, e in res["values_by_kind"].items() for r in e["rvas"]}
        pats = Counter()
        for site, target in lint.values:
            cover = lint.cover.get(site, {})
            sec, tsec = image.section_of(site), image.section_of(target)
            group, pat = pattern(image, prog, site, cover) if image.is_exec(site) else ("data", "")
            pats[(kind_of.get(site, "?"), sec[0] if sec else "?", "8-aligned" if site % 8 == 0 else "not 8-aligned", group)] += 1
            if args.list:
                print("    %s+0x%x  %-18s %s, rva %% 8 = %d, would point at rva 0x%x (%s)%s" % (
                    image.name, site, kind_of.get(site, "?"), sec[0] if sec else "?", site % 8, target,
                    tsec[0] if tsec else "the image base" if target == 0 else "the PE headers",
                    "" if not image.is_exec(site) else "; %s; decoded over it: %s" % (pat, ", ".join(
                        "0x%x %s" % (a, prog.decode(a).mnemonic) for a in sorted(cover)) or "nothing")))
        for (kind, sec, al, pat), n in sorted(pats.items()):
            print("    %4d  %-18s %-7s %-13s %s" % (n, kind, sec, al, pat))


if __name__ == "__main__":
    main()
