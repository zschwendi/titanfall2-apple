#!/usr/bin/env python3
"""Questions 4 and 5 for ui(11).dll: what loads it, and how its exports are reached.

One decode pass per host module. Functions are the unwind pieces from tools/scout/ui11_shape.py,
so a hot/cold split counts as one function.

  A. Strings: the format and path literals that matter (the pak path, the "(%02u).rpak" version
     format, the ".dll" suffix literal, the "%s__hidden" name builder, the "ui(" file names), and
     every function that reads them through a RIP-relative operand, with that function's imports.
  B. Loaders: every LoadLibrary* call through the IAT, the function holding it, and the header
     checks in that function (RPak magic, version word 7, flag byte and 3).
  C. Name resolution: GetProcAddress call sites split into literal names, dynamic names (taken from
     a parameter, the stack or a load) and immediates (ordinal-style).
  D. ui(11) export names present as NUL-delimited strings in each host's read-only data.
  E. The number of ui DLLs in the paks folder.

    .venv/bin/python -u tools/scout/ui11_loader.py [--game game/bin/x64_retail] [--ui game/r2/paks/Win64/ui(11).dll]
"""

import argparse
import collections
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))
sys.path.insert(0, HERE)
from pe_inventory import PE  # noqa: E402
import ui11_shape as S  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402
from capstone import x86 as X  # noqa: E402

HOSTS = ["engine.dll", "client.dll", "rtech_game.dll", "materialsystem_dx11.dll", "vguimatsurface.dll"]

KEYWORDS = {
    "pak path": re.compile(rb"paks[\\/]+Win64[\\/]+%s"),
    "pak version format": re.compile(rb"\(%0?2?u\)\.rpak"),
    "dll suffix literal": re.compile(rb"\x00\.dll\x00"),
    "hidden name builder": re.compile(rb"%s__hidden"),
    "ui file name": re.compile(rb"ui\(%|ui\(\d\d\)|\bui\.dll"),
}


def iat_map(pe):
    slots = {}
    rva, _ = pe.dirs[1]
    off = pe.rva_to_off(rva) if rva else None
    while off is not None and off + 20 <= len(pe.data):
        oft, _, _, nm, ft = struct.unpack_from("<IIIII", pe.data, off)
        if not (oft or nm or ft):
            break
        dll = (pe.cstr(nm) or "?").lower()
        ilt, i = oft or ft, 0
        while True:
            (v,) = struct.unpack_from("<Q", pe.data, pe.rva_to_off(ilt) + 8 * i)
            if v == 0:
                break
            fn = "#%d" % (v & 0xFFFF) if v >> 63 else (pe.cstr((v & 0x7FFFFFFF) + 2) or "?")
            slots[ft + 8 * i] = "%s!%s" % (dll, fn)
            i += 1
        off += 20
    return slots


def string_at(pe, rva):
    off = pe.rva_to_off(rva)
    end = pe.data.find(b"\0", off, off + 160)
    return pe.data[off:end].decode("latin-1") if end > off else ""


def find_strings(pe, pattern):
    out = set()
    for s in pe.sections:
        if pe.is_exec(s) or not s["rsize"]:
            continue
        blob = pe.data[s["rptr"]:s["rptr"] + s["rsize"]]
        for m in pattern.finditer(blob):
            start = blob.rfind(b"\0", 0, m.start()) + 1
            out.add(s["va"] + start)
    return out


def decode(pe, md, b, e):
    off = pe.rva_to_off(b)
    return list(md.disasm(pe.data[off:off + (e - b)], b))


def loader_checks(groups):
    magic = version = flags = False
    for insns in groups:
        for ins in insns:
            ops = ins.operands
            if ins.mnemonic == "cmp" and len(ops) == 2 and ops[1].type == X.X86_OP_IMM and ops[0].type == X.X86_OP_MEM:
                if ops[1].imm == 0x6B615052:
                    magic = True
                if ops[1].imm == 7 and ins.op_str.startswith("word"):
                    version = True
            if ins.mnemonic == "test" and len(ops) == 2 and ops[0].type == X.X86_OP_MEM \
                    and ops[1].type == X.X86_OP_IMM and ops[1].imm == 3 and "byte" in ins.op_str:
                flags = True
    return magic, version, flags


def scan_host(path, md, ui_names):
    pe = PE(path)
    primary, pieces, chained, exports = S.load_directory(pe)
    slots = iat_map(pe)
    export_by_rva = {e["rva"]: e["name"] for e in exports}
    name = os.path.basename(path)
    print("=" * 78)
    print("%s: %d exports, %d unwind functions (%d with split pieces)"
          % (name, len(exports), len(pieces), sum(len(v) > 1 for v in pieces.values())))

    targets = {}
    for label, pat in KEYWORDS.items():
        for rva in find_strings(pe, pat):
            targets[rva] = label
    reads = collections.defaultdict(list)     # target -> [(function, insn address, mnemonic)]
    fn_calls = collections.defaultdict(collections.Counter)
    load_sites = collections.defaultdict(list)
    load_groups = {}
    gpa = collections.Counter()
    hidden_fns = set()
    hidden_calls = collections.defaultdict(collections.Counter)

    for fs in sorted(pieces):
        groups = [decode(pe, md, b, e) for b, e in sorted(pieces[fs])]
        for insns in groups:
            for idx, ins in enumerate(insns):
                for o in ins.operands:
                    if o.type == X.X86_OP_MEM and o.mem.base == X.X86_REG_RIP:
                        t = ins.address + ins.size + o.mem.disp
                        if t in targets:
                            reads[t].append((fs, ins.address, ins.mnemonic))
                if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == X.X86_OP_MEM:
                    op = ins.operands[0]
                    if op.mem.base == X.X86_REG_RIP:
                        nm = slots.get(ins.address + ins.size + op.mem.disp, "[rip+global]")
                        fn_calls[fs][nm] += 1
                        if nm.startswith("kernel32.dll!LoadLibrary"):
                            load_sites[fs].append((ins.address, nm.split("!")[1]))
                        if nm.endswith("!GetProcAddress"):
                            og = S.origin(insns, idx, "rdx")
                            kind = "literal" if og[0] == "rip-address" else \
                                "immediate" if og[0] == "computed" else "dynamic"
                            gpa[kind] += 1
                    else:
                        fn_calls[fs]["[reg+0x%x]" % op.mem.disp] += 1
                        hidden_calls[fs]["[reg+0x%x]" % op.mem.disp] += 1

    # A. strings
    for t in sorted(targets):
        users = reads.get(t, [])
        print("  [%s] 0x%x %r: read by %d function(s)" % (targets[t], t, string_at(pe, t)[:44], len(users)))
        for fs, iaddr, mnem in sorted(set(users))[:4]:
            calls = fn_calls.get(fs, collections.Counter())
            top = ", ".join("%s x%d" % kv for kv in calls.most_common(5)) or "no calls"
            tag = " (export %s)" % export_by_rva[fs] if fs in export_by_rva else ""
            print("      %s at 0x%x in function 0x%x%s; calls: %s" % (mnem, iaddr, fs, tag, top))
            if targets[t] == "hidden name builder":
                hidden_fns.add(fs)

    # B. loaders
    for fs in sorted(load_sites):
        magic, version, flags = loader_checks([decode(pe, md, b2, e2) for b2, e2 in sorted(pieces[fs])])
        pieces_n = len(pieces[fs])
        print("  LoadLibrary in function 0x%x (%d piece%s): %s"
              % (fs, pieces_n, "" if pieces_n == 1 else "s", ", ".join("0x%x %s" % s for s in load_sites[fs])))
        print("      header checks in that function: RPak magic %s, version word 7 %s, flag byte & 3 %s"
              % (magic, version, flags))

    # C. name resolution
    print("  GetProcAddress call sites: literal name %d, dynamic name %d, immediate (ordinal-style) %d"
          % (gpa["literal"], gpa["dynamic"], gpa["immediate"]))

    # D. ui export names in read-only data
    blobs = [pe.data[s["rptr"]:s["rptr"] + s["rsize"]] for s in pe.sections if not pe.is_exec(s) and s["rsize"]]
    present = [n for n in ui_names if any(b"\0" + n.encode() + b"\0" in blob for blob in blobs)]
    print("  ui(11) export names present as NUL-delimited strings: %d of %d %s" % (len(present), len(ui_names), present[:3]))

    return {"name": name, "hidden_fns": hidden_fns, "hidden_calls": hidden_calls, "gpa": gpa, "pe": pe}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="game/bin/x64_retail")
    ap.add_argument("--ui", default="game/r2/paks/Win64/ui(11).dll")
    ap.add_argument("--folder", default="game/r2/paks/Win64")
    args = ap.parse_args()

    ui = PE(args.ui)
    _, _, _, ui_exports = S.load_directory(ui)
    ui_names = [e["name"] for e in ui_exports if e["name"]]
    print("ui(11) exports: %d named" % len(ui_names))
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    results = []
    for host in HOSTS:
        results.append(scan_host(os.path.join(args.game, host), md, ui_names))

    print("=" * 78)
    for r in results:
        if r["hidden_fns"]:
            print("%s functions that build '<name>__hidden': %s" % (r["name"], ", ".join("0x%x" % f for f in sorted(r["hidden_fns"]))))
            for f in sorted(r["hidden_fns"]):
                print("   0x%x calls through register-based slots: %s" % (f, dict(r["hidden_calls"][f])))
    print("GetProcAddress totals by host: %s" % {r["name"]: dict(r["gpa"]) for r in results})

    names = sorted(f for f in os.listdir(args.folder) if re.search(r"^ui(\(\d\d\))?\.dll$", f, re.I))
    print("ui DLLs in %s: %s" % (args.folder, names))


if __name__ == "__main__":
    main()
