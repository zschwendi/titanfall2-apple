#!/usr/bin/env python3
"""Scout: are the game's x87, MMX and 3DNow! instructions real code or mis-decoded data?

Sweeps every unwind-table (.pdata) range of the 24 game images (game/bin/x64_retail/*.dll
except d3dcompiler_46.dll, and game/server.dll) with Capstone, the census method. A site is
an instruction in Capstone's FPU, MMX or 3DNOW group, or with mnemonic emms, femms, fxsave,
fxrstor, fninit, wait or fwait. Sites are grouped by the function they sit in (the primary
.pdata entry; split fragments join their parent, as in cpu_core).

Evidence per site:
  reached     recursive descent from the function entry reaches the instruction
  in_table    the instruction lies inside a jump table that a dispatch reached by descent reads
  garbage     the linear sweep met undecodable bytes within 24 bytes of it
Evidence per function:
  entry_ref   the entry is called or jumped to from another function, named by a RIP-relative
              operand, stored as an absolute pointer (base relocation), or exported
  complete    descent met no unresolved indirect jump (otherwise its reach is a lower bound)
  conflicts   x87 stack-depth merge conflicts over the reached instructions

Two repairs to the shared decoder (cpu_core), made here without editing it. The switch resolver
is replaced by resolve_switch_general, which takes the table base from the displacement of the
dispatch load and handles the two-level byte-map form. Prefixed mnemonics (`repz ret`,
`notrack jmp`) are reduced to the instruction. A resolved table is used as evidence of data only
when every one of its entries lands in the function's ranges; a partial table still supplies its
in-range targets to the descent, but its bytes are not called data.

Rules (classify()), in order: a reached site that is not a bare wait, with a referenced entry
and no depth conflict, is real. A site inside a jump table read by a reached dispatch is data.
Sites all inside undecodable bytes with nothing referring to the entry are data. A complete
descent from an unreferenced entry that reaches no site is data. Everything else is
undetermined, with the basis and reason. Branch targets are not evidence: a direct branch can
be decoded from data just as easily.

Nothing is executed. Output is addresses, counts and mnemonics, never instruction bytes.

    .venv/bin/python tools/scout/x87_scan.py game
    .venv/bin/python tools/scout/x87_scan.py game --functions
"""

import argparse
import bisect
import collections
import os
import struct
import sys
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))
sys.path.insert(0, HERE)
import cpu_core  # noqa: E402
from cpu_core import Dis, descend, function_table, game_images, load  # noqa: E402

from capstone import x86_const as X  # noqa: E402

EXTRA_MN = {"emms", "femms", "fxsave", "fxrstor", "fxsave64", "fxrstor64", "fninit", "wait", "fwait"}
BARE_WAIT = {"wait", "fwait"}
GARBAGE_RADIUS = 24

# Net x87 stack effect for the depth dataflow.
PUSH = {"fld", "fild", "fldz", "fld1", "fldpi", "fldl2e", "fldl2t", "fldlg2", "fldln2", "fbld"}
POP1 = {"fstp", "fistp", "fisttp", "fbstp"}
POP2 = {"fcompp", "fucompp"}
POP_ON_ARITH = {"faddp", "fsubp", "fsubrp", "fmulp", "fdivp", "fdivrp", "fcomp", "fucomp", "fcomip",
                "fucomip", "fcompi", "fucompi"}
PRIVILEGED = {"in", "out", "hlt", "cli", "sti", "lgdt", "lidt", "ltr", "invd", "wbinvd", "rdmsr", "wrmsr",
              "syscall", "sysret", "sysenter", "sysexit", "iret", "iretq"}


def is_site(ci):
    g = ci.groups
    return (X.X86_GRP_FPU in g or X.X86_GRP_MMX in g or X.X86_GRP_3DNOW in g or ci.mnemonic in EXTRA_MN)


def site_kind(mn):
    """'x87', 'mmx', '3dnow' or 'wait' for a site mnemonic (for the totals)."""
    if mn in BARE_WAIT:
        return "wait"
    if mn in ("femms", "pi2fd", "pi2fw", "pfadd", "pfsub", "pfmul"):
        return "3dnow"
    if mn.startswith("p") or mn in ("emms",):
        return "mmx"
    return "x87"


def _last_writer(seq, upto, fam):
    """Index in seq of the last instruction before upto that writes register family fam."""
    for k in range(upto - 1, -1, -1):
        if fam in seq[k].wr:
            return k
    return None


def _const_value(seq, upto, fam):
    """RVA value of family fam before seq[upto]: the RVA after `lea fam, [rip+X]` plus X, or an
    immediate moved into it. None when the value is not a known constant."""
    k = _last_writer(seq, upto, fam)
    if k is None:
        return None
    ins = seq[k]
    if ins.mn == "lea" and len(ins.ops) == 2 and ins.ops[1][0] == "m" and ins.ops[1][6] == "rip":
        return ins.rva + ins.size + ins.ops[1][4]
    if ins.mn == "mov" and len(ins.ops) == 2 and ins.ops[1][0] == "i":
        return ins.ops[1][1] & 0xFFFFFFFF
    return None


def _index_chain(seq, upto, fam):
    """Register families an index value may have been copied from before seq[upto]: the family
    itself, and while its last write is a register-to-register move or extension (movsxd, movzx,
    mov), the source of that move, a few steps back. The bound is compared on any of them."""
    fams = {fam}
    for _ in range(3):
        k = _last_writer(seq, upto, fam)
        if k is None:
            break
        ins = seq[k]
        if ins.mn not in ("movsxd", "movzx", "movsx", "mov") or len(ins.ops) != 2 or ins.ops[1][0] != "r":
            break
        fam = ins.ops[1][1]
        fams.add(fam)
        upto = k
    return fams


def _cmp_bound(seq, upto, fams):
    """Case count implied by the closest `cmp f, imm` before upto, for any family f in fams:
    imm + 1, or None."""
    for k in range(upto - 1, -1, -1):
        ins = seq[k]
        if ins.mn == "cmp" and len(ins.ops) == 2 and ins.ops[0][0] == "r" and ins.ops[0][1] in fams \
                and ins.ops[1][0] == "i":
            n = ins.ops[1][1] + 1
            return n if 0 < n <= 4096 else None
    return None


def _prev_linear(dis, rva, ranges, keep):
    """cpu_core.prev_linear, with the linear sweep of each range cached on the decoder, since a
    function can hold several dispatches and each call would otherwise sweep the whole range."""
    cache = dis.__dict__.setdefault("_x87_linear", {})
    for b, e in ranges:
        if b <= rva < e:
            if (b, e) not in cache:
                cache[(b, e)] = dis.linear(b, e)
            return [i for i in cache[(b, e)] if i.rva < rva][-keep:]
    return []


def resolve_switch_general(dis, jmp, ranges, span=160):
    """MSVC x64 switch dispatch, general form. Replaces cpu_core.resolve_switch.

    The dispatch is `load r, [base + idx*4 + T]; add r, src; jmp r`, where the base and src
    registers hold RVAs set by `lea reg, [rip+X]`, and T is a displacement on the load. The
    table base is the register value plus T, so a lea that yields 0 is still correct. Entries
    are signed 32-bit offsets from src. Two-level dispatch (a byte map indexed first, then the
    dword table) is also handled. The case count comes from the closest `cmp idx, imm` before
    the load (or before the byte-map load). Returns the case targets inside ranges, or None
    when any part of the pattern is not recognised."""
    seq = _prev_linear(dis, jmp.rva, ranges, keep=span)
    if not seq or not jmp.ops or jmp.ops[0][0] != "r":
        return None
    reg = jmp.ops[0][1]
    add_i = _last_writer(seq, len(seq), reg)
    if add_i is None:
        return None
    add = seq[add_i]
    if add.mn != "add" or len(add.ops) != 2 or add.ops[0][0] != "r" or add.ops[1][0] != "r":
        return None
    src_fam = add.ops[1][1]
    load_i = _last_writer(seq, add_i, reg)
    if load_i is None:
        return None
    load = seq[load_i]
    if load.mn not in ("mov", "movsxd", "movzx", "movsx") or len(load.ops) != 2 or load.ops[1][0] != "m":
        return None
    _, base_fam, idx_fam, scale, disp, _, _, size = load.ops[1]
    if scale != 4 or size != 4 or idx_fam is None or base_fam is None:
        return None
    table = _const_value(seq, load_i, base_fam)
    src_val = _const_value(seq, add_i, src_fam)
    if table is None or src_val is None:
        return None
    table += disp
    idx_k = _last_writer(seq, load_i, idx_fam)
    byte_map = None
    if idx_k is not None:
        w = seq[idx_k]
        if w.mn in ("movzx", "movsx", "mov") and len(w.ops) == 2 and w.ops[1][0] == "m" and w.ops[1][7] == 1:
            _, mb, mi, _, mdisp, _, _, _ = w.ops[1]
            mval = _const_value(seq, idx_k, mb) if mb is not None else None
            mbound = _cmp_bound(seq, idx_k, _index_chain(seq, idx_k, mi)) if mi is not None else None
            if mval is None or mbound is None:
                return None
            byte_map = (mval + mdisp, mbound)
    if byte_map is None:
        bound = _cmp_bound(seq, load_i, _index_chain(seq, load_i, idx_fam))
        if bound is None:
            return None
        indices = range(bound)
    else:
        mstart, bound = byte_map
        raw = dis.bytes_at(mstart, bound)
        if len(raw) < bound:
            return None
        indices = sorted(set(raw))
    targets = set()
    extents = []
    inrange = 0
    if byte_map is not None:
        extents.append((byte_map[0], byte_map[1]))
    for i in indices:
        raw = dis.bytes_at(table + 4 * i, 4)
        if len(raw) < 4:
            return None
        extents.append((table + 4 * i, 4))
        (rel,) = struct.unpack("<i", raw)
        t = (src_val + rel) & 0xFFFFFFFF
        if any(b <= t < e for b, e in ranges):
            targets.add(t)
            inrange += 1
    SWITCH_STATS[(dis.pe.path, jmp.rva)] = (len(indices), inrange)
    # Only a table whose every entry lands in the function is taken as data. A partial table
    # may be one entry too long, so the bytes it covers are not called data.
    if inrange == len(indices):
        SWITCH_TABLES.setdefault((dis.pe.path, jmp.rva), []).extend(extents)
    return sorted(targets) or None


_PREFIX_WORDS = {"repz", "repnz", "rep", "repe", "repne", "notrack", "bnd", "lock", "data16", "addr32",
                 "xacquire", "xrelease", "cs", "ds", "es", "ss", "fs", "gs", "rex64"}
_make_ins_capstone = cpu_core.make_ins


def _make_ins_plain(md, ci, names):
    """cpu_core tests mnemonics with startswith and equality. Capstone prints prefixed
    instructions as one string (`repz ret`, `notrack jmp`), which would hide a return or an
    indirect jump from descend. Strip the prefix words so the mnemonic is the instruction."""
    ins = _make_ins_capstone(md, ci, names)
    words = ins.mn.split()
    if len(words) > 1 and all(w in _PREFIX_WORDS for w in words[:-1]):
        ins.mn = words[-1]
    return ins


cpu_core.make_ins = _make_ins_plain
# (image path, jump RVA) -> [(start RVA, length)] of the table bytes that dispatch reads.
SWITCH_TABLES = {}
# (image path, jump RVA) -> (entries read, entries that land in the function's ranges).
SWITCH_STATS = {}
cpu_core.resolve_switch = resolve_switch_general


def branch_target(ci):
    if ci.operands and ci.operands[0].type == X.X86_OP_IMM:
        return ci.operands[0].imm & 0xFFFFFFFFFFFFFFFF
    return None


def rip_target(ci):
    for op in ci.operands:
        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
            return ci.address + ci.size + op.mem.disp
    return None


class Image:
    """Everything the classifier needs about one image, built inside one worker process."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.pe = load(path)
        self.entries, self.funcs = function_table(self.pe)
        self.dis = Dis(self.pe)
        self.calls_ext = collections.Counter()    # direct branch target that is a function entry, from another function
        self.riprefs = collections.Counter()      # RIP-relative operand target -> count
        self.ptrs = set()                         # RVAs stored as absolute pointers (base relocations)
        self.exports = set()
        self.sites = []                           # dicts: rva, mn, text, func (parent entry)
        self.resync_rvas = []                     # where the linear sweep met undecodable bytes
        self.resync_by_func = collections.Counter()
        self.insns = 0

    def sweep(self):
        md = self.dis.md
        for b, e, parent in self.entries:
            buf = self.dis.span(b, e)
            pos = 0
            while pos < len(buf):
                last = pos
                for ci in md.disasm(buf[pos:], b + pos):
                    self.insns += 1
                    last = (ci.address - b) + ci.size
                    if ci.mnemonic in ("call", "jmp") or X.X86_GRP_JUMP in ci.groups:
                        bt = branch_target(ci)
                        if bt is not None:
                            if bt in self.funcs and bt != parent:
                                self.calls_ext[bt] += 1
                    rt = rip_target(ci)
                    if rt is not None:
                        self.riprefs[rt] += 1
                    if is_site(ci):
                        self.sites.append({"rva": ci.address, "mn": ci.mnemonic, "text": ci.op_str,
                                           "func": parent})
                if last >= len(buf):
                    break
                self.resync_rvas.append(b + last)
                self.resync_by_func[parent] += 1
                pos = last + 1
        self.resync_rvas.sort()

    def near_garbage(self, rva):
        i = bisect.bisect_left(self.resync_rvas, rva - GARBAGE_RADIUS)
        return i < len(self.resync_rvas) and self.resync_rvas[i] <= rva + GARBAGE_RADIUS

    def relocation_pointers(self):
        pe = self.pe
        rva, size = pe.dirs[5]
        off = pe.rva_to_off(rva) if rva else None
        if off is None or size == 0:
            return
        end = off + size
        p = off
        while p + 8 <= end and p + 8 <= len(pe.data):
            page, block = struct.unpack_from("<II", pe.data, p)
            if block < 8:
                break
            count = (block - 8) // 2
            for k in range(count):
                (entry,) = struct.unpack_from("<H", pe.data, p + 8 + 2 * k)
                typ, lo = entry >> 12, entry & 0xFFF
                if typ not in (10, 3):
                    continue
                so = pe.rva_to_off(page + lo)
                if so is None:
                    continue
                if typ == 10 and so + 8 <= len(pe.data):
                    (val,) = struct.unpack_from("<Q", pe.data, so)
                elif typ == 3 and so + 4 <= len(pe.data):
                    (val,) = struct.unpack_from("<I", pe.data, so)
                else:
                    continue
                self.ptrs.add((val - pe.image_base) & 0xFFFFFFFF)
            p += block

    def export_rvas(self):
        pe = self.pe
        rva, size = pe.dirs[0]
        off = pe.rva_to_off(rva) if rva else None
        if off is None or off + 40 > len(pe.data):
            return
        _, _, _, _, _, _, nfunc, _, addr_f, _, _ = struct.unpack_from("<IIHHIIIIIII", pe.data, off)
        aoff = pe.rva_to_off(addr_f)
        if aoff is None:
            return
        for i in range(min(nfunc, 100000)):
            if aoff + 4 * i + 4 > len(pe.data):
                break
            (fr,) = struct.unpack_from("<I", pe.data, aoff + 4 * i)
            self.exports.add(fr)


def x87_depth(insns, succ, seeds):
    """Depth dataflow over the descent CFG from the seeds (the entry), depth 0 at the seed.
    Returns (merge_conflicts, underflows, max_depth). A pop at depth 0 is an underflow; a
    routine may take st(0) from its caller, so underflows are reported, not used to reject a
    function."""
    depth = {}
    conflicts = underflows = 0
    max_depth = 0
    work = []
    for seed in seeds:
        if seed in insns and seed not in depth:
            depth[seed] = 0
            work.append(seed)
    while work:
        a = work.pop()
        ins = insns.get(a)
        if ins is None:
            continue
        d = depth[a]
        mn = ins.mn
        if mn in PUSH:
            nd = d + 1
        elif mn in POP1 or mn in POP_ON_ARITH:
            if d == 0:
                underflows += 1
            nd = max(d - 1, 0)
        elif mn in POP2:
            if d < 2:
                underflows += 1
            nd = max(d - 2, 0)
        else:
            nd = d
        max_depth = max(max_depth, nd)
        for s in succ.get(a, []):
            if s in depth:
                if depth[s] != nd:
                    conflicts += 1
                continue
            depth[s] = nd
            work.append(s)
    return conflicts, underflows, max_depth


def analyse_function(img, parent, sites_here):
    ranges = img.funcs.get(parent, [(parent, parent + 1)])
    # Reach starts only at the entry. Fragments of a split function are followed when a direct
    # branch or fall-through leads into them; seeding every .pdata range start would count a
    # fragment that nothing branches to, and those fragments decode as data in practice.
    insns, succ, unresolved = descend(img.dis, ranges, [parent])
    # Jump tables read by dispatches the descent reached: their bytes are data.
    tables = []
    for a, ins in insns.items():
        if ins.is_indirect_jmp:
            for start, length in SWITCH_TABLES.get((img.path, a), []):
                tables.append((start, length, a))
    sites = []
    for s in sites_here:
        s = dict(s)
        s["reached"] = s["rva"] in insns
        s["in_table"] = next((a for st, ln, a in tables if st <= s["rva"] < st + ln), None)
        s["garbage"] = img.near_garbage(s["rva"])
        s["kind"] = site_kind(s["mn"])
        s["bare_wait"] = s["mn"] in BARE_WAIT
        sites.append(s)
    conflicts, underflows, max_depth = x87_depth(insns, succ, [parent])
    return {
        "parent": parent,
        "ranges": ranges,
        "sites": sites,
        "descent_insns": len(insns),
        "unresolved": len(unresolved),
        "complete": len(unresolved) == 0,
        "resync": img.resync_by_func.get(parent, 0),
        "conflicts": conflicts,
        "underflows": underflows,
        "max_depth": max_depth,
        "priv_reached": sum(1 for ci in insns.values() if ci.mn in PRIVILEGED),
        "entry_ref": (img.calls_ext.get(parent, 0) > 0 or img.riprefs.get(parent, 0) > 0
                      or parent in img.ptrs or parent in img.exports),
        "calls_ext": img.calls_ext.get(parent, 0),
        "rip_refs": img.riprefs.get(parent, 0),
        "ptr_ref": parent in img.ptrs,
        "exported": parent in img.exports,
    }


def classify(f):
    """(class, basis, reason). See the module docstring for the rules. Only descent reach
    counts as evidence of code: a direct branch can be decoded from data just as easily, so
    branch targets are not used. basis names the rule that settled the function."""
    sites = f["sites"]
    reached = [s for s in sites if s["reached"] and not s["bare_wait"]]
    tabled = [s for s in sites if s["in_table"] is not None]
    if reached and any(s["in_table"] is not None for s in reached):
        return "undetermined", "conflict", "a reached instruction lies inside a jump table; the two readings conflict"
    if tabled and len(tabled) == len(sites):
        where = sorted({"%x" % s["in_table"] for s in tabled})
        return "data", "jump table", ("all %d site(s) lie inside jump table(s) read by the dispatch at %s"
                                      % (len(sites), ", ".join(where)))
    if reached:
        near = sum(1 for s in reached if s["garbage"])
        note = "" if not near else "; %d of them lie within 24 bytes of undecodable bytes" % near
        if not f["entry_ref"]:
            return "undetermined", "reached, unreferenced", (
                "%d site(s) reached from the entry, but nothing refers to the entry%s" % (len(reached), note))
        if f["conflicts"]:
            return "undetermined", "depth conflict", (
                "%d site(s) reached and the entry is referenced, but the x87 depth conflicts at %d merge "
                "point(s)%s" % (len(reached), f["conflicts"], note))
        return "real", "reached", ("%d site(s) reached from a referenced entry, depth consistent%s"
                                   % (len(reached), note))
    if all(s["garbage"] for s in sites) and not f["entry_ref"]:
        return "data", "undecodable", "every site lies within 24 bytes of undecodable bytes; nothing refers to the entry"
    if f["complete"] and not f["entry_ref"] and not any(s["reached"] for s in sites):
        return "data", "unreached", "descent from a complete entry reaches no site; nothing refers to the entry"
    if not f["complete"]:
        return "undetermined", "unresolved dispatch", ("descent stops at %d unresolved indirect jump(s); no site "
                                                       "reached" % f["unresolved"])
    if f["entry_ref"]:
        return "undetermined", "unreached, referenced", "entry referenced, descent complete, but no site reached"
    return "undetermined", "unreached, unreferenced", (
        "no site reached and the entry is unreferenced, but some sites are not near undecodable bytes")


def scan_image(path):
    img = Image(path)
    img.sweep()
    img.relocation_pointers()
    img.export_rvas()
    by_func = collections.defaultdict(list)
    for s in img.sites:
        by_func[s["func"]].append(s)
    funcs = [analyse_function(img, parent, ss) for parent, ss in sorted(by_func.items())]
    return {"image": img.name, "insns": img.insns, "sites": img.sites, "functions": funcs}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="game directory")
    ap.add_argument("--functions", action="store_true", help="print one line per function")
    args = ap.parse_args()
    paths = [p for p in game_images(args.root) if os.path.basename(p).lower() != "d3dcompiler_46.dll"]
    with Pool(4) as pool:
        results = pool.map(scan_image, paths)

    total_sites = sum(len(r["sites"]) for r in results)
    total_funcs = sum(len(r["functions"]) for r in results)
    print("%d images, %d instructions swept, %d x87/MMX/3DNow! sites in %d functions"
          % (len(results), sum(r["insns"] for r in results), total_sites, total_funcs))

    cls_fn = collections.Counter()
    cls_sites = collections.Counter()
    basis_fn = collections.Counter()
    basis_sites = collections.Counter()
    real_mn = collections.Counter()
    real_mod = collections.Counter()
    real_funcs = collections.defaultdict(set)
    real_max_depth = 0
    real_sites_by_kind = collections.Counter()
    for r in results:
        name = r["image"]
        print("%-26s insns=%8d sites=%4d functions=%3d" % (name, r["insns"], len(r["sites"]), len(r["functions"])))
        for f in r["functions"]:
            c, basis, why = classify(f)
            cls_fn[c] += 1
            cls_sites[c] += len(f["sites"])
            basis_fn[(c, basis)] += 1
            basis_sites[(c, basis)] += len(f["sites"])
            if c == "real":
                real_funcs[name].add(f["parent"])
                real_max_depth = max(real_max_depth, f["max_depth"])
                for s in f["sites"]:
                    if s["reached"] and not s["bare_wait"]:
                        real_mn[s["mn"]] += 1
                        real_mod[name] += 1
                        real_sites_by_kind[s["kind"]] += 1
            if args.functions:
                # Tab-separated, one function per line: F, module, entry, class, basis, references,
                # sites as "address:mnemonic:tag" (R reached, T in a jump table, - neither), reason.
                sites_str = " ".join("%x:%s:%s" % (s["rva"], s["mn"], "R" if s["reached"]
                                                   else ("T" if s["in_table"] is not None else "-"))
                                     for s in f["sites"])
                refs = "calls=%d rip=%d ptr=%d exp=%d" % (f["calls_ext"], f["rip_refs"], int(f["ptr_ref"]),
                                                          int(f["exported"]))
                print("F\t%s\t%06x\t%s\t%s\t%s\t%s\t%s" % (name, f["parent"], c, basis, refs, sites_str, why))

    print("\nclass            functions   sites")
    for c in ("real", "data", "undetermined"):
        print("%-16s %9d %7d" % (c, cls_fn[c], cls_sites[c]))
    print("\nbasis            class          functions   sites")
    for (c, basis), n in sorted(basis_fn.items(), key=lambda kv: (kv[0][0], -kv[1])):
        print("%-16s %-14s %9d %7d" % (basis, c, n, basis_sites[(c, basis)]))
    print("\nreal x87 sites by mnemonic: %s" % dict(real_mn.most_common()))
    print("real sites by module: %s" % dict(real_mod.most_common()))
    print("real functions: %d in %d modules" % (sum(len(v) for v in real_funcs.values()), len(real_funcs)))
    print("deepest x87 stack reached in any real function: %d" % real_max_depth)
    print("reached sites by kind (x87 / mmx / 3dnow): %s" % dict(real_sites_by_kind))
    if args.functions:
        print("\nF lines: one per function; sites are address:mnemonic:tag (R reached from the entry,"
              " T inside a jump table, - neither)")


if __name__ == "__main__":
    main()
