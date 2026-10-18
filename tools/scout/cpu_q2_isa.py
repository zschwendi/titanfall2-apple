#!/usr/bin/env python3
"""Question 2: how is the AVX/AVX2/AES/SHA/BMI2/PCLMUL code in engine.dll reached?

Steps, all on engine.dll:
  1. Coverage. Which executable bytes lie outside .pdata ranges? Linear-decode those gaps and
     count the ISA instructions in them, so code the census could not see is not missed.
  2. Graph. Decode every .pdata function once. Collect direct call and jump edges, RIP-relative
     address computations (lea) into code, and code pointers stored in data (base relocations).
     Collect exports.
  3. Each function that contains ISA instructions: exported? address-taken (lea or relocation)?
     direct callers? the ISA groups it holds.
  4. Gating. The flag words that cpuid code writes (the rip-relative stores inside the
     functions that contain cpuid, from cpu_sites.py output). For every direct call or address
     materialisation of an ISA function, test whether the site is dominated by a conditional
     branch that reads one of those flag words. Dominance here: remove the branch's edge to
     one successor and see whether the site is still reachable from the entry.

    .venv/bin/python tools/scout/cpu_q2_isa.py <scratchpad>/sites.json game
"""

import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import (Dis, ISA_GROUPS, RangeIndex, descend, function_table, game_images,  # noqa: E402
                      load, make_ins, preds)

ISA = {"avx", "avx2", "aes", "sha", "bmi2", "pclmul"}


def exports_map(pe):
    """name -> RVA for the export directory."""
    rva, size = pe.dirs[0]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return {}
    _, _, _, _, _, _, nfunc, nnames, aof, aonof, aoo = struct.unpack_from("<IIHHIIIIIII", pe.data, off)
    out = {}
    for i in range(nnames):
        (nrva,) = struct.unpack_from("<I", pe.data, pe.rva_to_off(aonof) + 4 * i)
        (ordi,) = struct.unpack_from("<H", pe.data, pe.rva_to_off(aoo) + 2 * i)
        (frva,) = struct.unpack_from("<I", pe.data, pe.rva_to_off(aof) + 4 * ordi)
        out[pe.cstr(nrva)] = frva
    return out


def code_sections(pe):
    return [(s["va"], s["va"] + s["vsize"], s) for s in pe.sections if pe.is_exec(s)]


def relocated_code_pointers(pe, exec_ranges):
    """{target_rva: [site_rva, ...]} for DIR64 relocations whose value points into code."""
    rva, size = pe.dirs[5]
    off = pe.rva_to_off(rva) if rva else None
    out = collections.defaultdict(list)
    if off is None:
        return out
    end = off + size
    while off + 8 <= end:
        page, block = struct.unpack_from("<II", pe.data, off)
        if block < 8:
            break
        n = (block - 8) // 2
        for e in struct.unpack_from("<%dH" % n, pe.data, off + 8):
            if e >> 12 == 10:
                site = page + (e & 0xFFF)
                so = pe.rva_to_off(site)
                if so is None:
                    continue
                (v,) = struct.unpack_from("<Q", pe.data, so)
                t = v - pe.image_base
                if any(a <= t < b for a, b, _ in exec_ranges):
                    out[t].append(site)
        off += block
    return out


def gap_scan(pe, dis, entries, exec_ranges):
    """Linear decode of executable bytes outside every .pdata range."""
    cov = sorted((b, e) for b, e, _ in entries)
    gaps = []
    for a, b, s in exec_ranges:
        cur = a
        for cb, ce in cov:
            if ce <= cur or cb >= b:
                continue
            if cb > cur:
                gaps.append((cur, min(cb, b)))
            cur = max(cur, ce)
        if cur < b:
            gaps.append((cur, b))
    total = sum(e - b for b, e in gaps)
    isa = collections.Counter()
    mn = collections.Counter()
    where = []
    for b, e in gaps:
        if e - b < 1:
            continue
        ins = dis.linear(b, e)
        for i in ins:
            mn[i.mn] += 1
            for g in i.grp:
                if g in ISA:
                    isa[g] += 1
        if any(g in ISA for i in ins for g in i.grp) and len(where) < 20:
            where.append((b, e, sum(1 for i in ins if i.grp & ISA)))
        dis.cache_clear()
    return {"gaps": len(gaps), "gap_bytes": total, "isa": dict(isa), "mn": mn, "where": where,
            "gap_list": gaps}


def unit_of(rva, units):
    """Map an RVA to the unit (pdata function key or gap start) that contains it."""
    return units.index_of(rva)


class Units:
    """pdata functions and unwind-less gaps, indexed by address."""

    def __init__(self, funcs, gaps):
        self.begin = {}
        self.ranges = {}
        for key, rs in funcs.items():
            self.ranges["f%x" % key] = rs
        for b, e in gaps:
            self.ranges["g%x" % b] = [(b, e)]
        self.spans = sorted((b, e, label) for label, rs in self.ranges.items() for b, e in rs)
        self.starts = [x[0] for x in self.spans]

    def index_of(self, rva):
        import bisect
        i = bisect.bisect_right(self.starts, rva) - 1
        while i >= 0 and i > len(self.starts) - 1000000:
            b, e, label = self.spans[i]
            if b <= rva < e:
                return label
            if rva - b > 0x200000:
                break
            i -= 1
        return None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sites = json.load(open(sys.argv[1]))
    path = [p for p in game_images(sys.argv[2]) if os.path.basename(p) == "engine.dll"][0]
    pe = load(path)
    entries, funcs = function_table(pe)
    dis = Dis(pe)
    exec_ranges = code_sections(pe)
    exports = exports_map(pe)
    export_names = collections.defaultdict(list)
    for n, r in exports.items():
        export_names[r].append(n)

    # 1. coverage -------------------------------------------------------------
    gap = gap_scan(pe, dis, entries, exec_ranges)
    print("1. Coverage of engine.dll code by .pdata")
    print("   executable sections:", [(s["name"], hex(a), hex(b - a)) for a, b, s in exec_ranges])
    print("   bytes outside .pdata ranges: %d in %d gaps" % (gap["gap_bytes"], gap["gaps"]))
    print("   ISA instructions in gaps (linear decode):", gap["isa"] or "none")
    print("   top mnemonics in gaps:", gap["mn"].most_common(12))
    print("   gaps holding ISA instructions (start, end, count):",
          ["%x-%x:%d" % w for w in gap["where"]][:20] or "none")
    gap_ranges = [(b, e) for b, e in gap["gap_list"] if e > b]
    units = Units(funcs, gap_ranges)

    # 2. graph -----------------------------------------------------------------
    relocs = relocated_code_pointers(pe, exec_ranges)
    slot_sites = sorted({x for v in relocs.values() for x in v})
    import bisect as _bis
    def near_slot(a):
        i = _bis.bisect_left(slot_sites, a - 0x400)
        return i < len(slot_sites) and slot_sites[i] <= a + 0x400
    rip_refs = []                                # (target, unit, rva, mn) for refs near a relocated slot
    rip_loads = collections.defaultdict(set)     # unit -> rip-relative globals it reads
    rip_stores = collections.defaultdict(set)    # unit -> rip-relative globals it writes
    fn_isa = {}                                  # unit label -> ISA group counts
    calls = collections.defaultdict(set)         # target unit -> caller units
    callees = collections.defaultdict(set)       # caller unit -> target units
    call_sites = collections.defaultdict(list)   # target unit -> [(caller unit, call rva)]
    lea_units = collections.defaultdict(list)    # target unit -> [(caller unit, site rva)]
    reloc_units = set()
    ind_calls = 0
    all_units = [("f%x" % k, rs) for k, rs in funcs.items()] + [("g%x" % b, [(b, e)]) for b, e in gap_ranges]
    for label, ranges in all_units:
        isa_here = collections.Counter()
        for b, e in ranges:
            for ins in dis.linear(b, e):
                for g in ins.grp:
                    if g in ISA:
                        isa_here[g] += 1
                if ins.mn == "call":
                    if ins.target is not None:
                        tu = units.index_of(ins.target)
                        if tu:
                            calls[tu].add(label)
                            callees[label].add(tu)
                            call_sites[tu].append((label, ins.rva))
                    else:
                        ind_calls += 1
                elif ins.mn == "jmp" and ins.target is not None:
                    tu = units.index_of(ins.target)
                    if tu and tu != label:
                        calls[tu].add(label)
                        callees[label].add(tu)
                        call_sites[tu].append((label, ins.rva))
                for o in ins.ops:
                    if o[0] == "m" and o[6] == "rip":
                        t = ins.rva + ins.size + o[4]
                        if near_slot(t):
                            rip_refs.append((t, label, ins.rva, ins.mn))
                        if o is ins.ops[0] and ins.mn.startswith("mov") and len(ins.ops) == 2:
                            rip_stores[label].add(t)
                        else:
                            rip_loads[label].add(t)
                if ins.mn == "lea":
                    for o in ins.ops:
                        if o[0] == "m" and o[6] == "rip":
                            t = ins.rva + ins.size + o[4]
                            tu = units.index_of(t)
                            if tu:
                                lea_units[tu].append((label, ins.rva))
        if isa_here:
            fn_isa[label] = dict(isa_here)
        dis.cache_clear()
    for t in relocs:
        tu = units.index_of(t)
        if tu:
            reloc_units.add(tu)
    root_units = set()
    for r in exports.values():
        tu = units.index_of(r)
        if tu:
            root_units.add(tu)
    tu = units.index_of(pe.entry_rva) if pe.entry_rva else None
    if tu:
        root_units.add(tu)
    begins = {units.ranges[l][0][0]: l for l in units.ranges}
    reloc_begin = {t: sites_ for t, sites_ in relocs.items() if t in begins}
    lea_begin = {t: v for t, v in lea_units.items() if t in begins}
    addr_taken = set(begins[t] for t in reloc_begin) | set(begins[t] for t in lea_begin)
    root_units = root_units | addr_taken
    print("\n2. Graph (direct edges; gaps decoded linearly as extra units)")
    print("   units: %d pdata functions + %d gaps; direct-call edges into units: %d; indirect call sites: %d" % (
        len(funcs), len(gap_ranges), len(calls), ind_calls))
    print("   code pointers in data (relocations into code): %d units; lea references into code: %d units" % (
        len(reloc_units), len(lea_units)))
    print("   exports: %d names -> units %s; entry point unit: %s" % (
        len(exports), sorted(root_units)[:4], tu))

    # 3. ISA units and reachability from roots -----------------------------------
    print("\n3. Units containing ISA instructions (%d)" % len(fn_isa))
    rows = []
    for label in sorted(fn_isa, key=lambda l: int(l[1:], 16)):
        rs = units.ranges[label]
        size = sum(e - b for b, e in rs)
        rows.append((label, size, fn_isa[label]))
    tot = collections.Counter()
    for _, _, c in rows:
        tot.update(c)
    print("   by group, summed over units:", dict(sorted(tot.items())))
    print("   %-9s %-7s %-34s %-5s %-6s %-7s %-6s %s" % ("unit", "bytes", "isa groups", "kind", "export",
                                                     "addrtk", "callers", "caller units"))

    def ancestors_reach(u):
        seen, work, roots = {u}, [u], set()
        while work:
            x = work.pop()
            if x in root_units or x in reloc_units:
                roots.add(x)
            for c in calls.get(x, ()):
                if c not in seen:
                    seen.add(c)
                    work.append(c)
        return roots

    reach_info = {}
    for label, size, c in rows:
        b = int(label[1:], 16)
        kind = "pdata" if label[0] == "f" else "gap"
        ex = ",".join(export_names.get(b, [])) or "-"
        at = label and (units.index_of(b) in addr_taken)
        cl = sorted(calls.get(label, ()))
        reach = ancestors_reach(label) if label != None else set()
        reach_info[label] = reach
        print("   %-9s %-7d %-34s %-5s %-6s %-7s %-6d %s" % (
            label, size, ",".join("%s:%d" % kv for kv in sorted(c.items())), kind,
            "yes" if ex != "-" else "no", "yes" if at else "no", len(cl),
            " ".join(cl[:5]) + (" ..." if len(cl) > 5 else "")))
    reach_roots = {k: sorted(v) for k, v in reach_info.items() if v}
    print("   units with an ISA group that have a path from an export, entry point, or address-taken unit: %d of %d" % (
        len(reach_roots), len(rows)))
    print("   units with no such path (no direct caller chain reaches a root): %s" % (
        [l for l, _, _ in rows if not reach_info[l]]))

    # 4. flag words and gating --------------------------------------------------
    flag_words = set()
    for s in sites["sites"]:
        if s["img"] == "engine.dll" and s["mn"] == "cpuid":
            ranges = funcs.get(s["func"], [(s["func"], s["func"] + 1)])
            insns, succ, _ = descend(dis, ranges, [b for b, _ in ranges])
            for rva, ins in insns.items():
                if ins.mn in ("mov", "and", "or", "btr", "bts") and ins.ops and ins.ops[0][0] == "m" \
                        and ins.ops[0][6] == "rip":
                    flag_words.add(rva + ins.size + ins.ops[0][4])
    print("\n4. cpuid flag words (rip-relative stores inside cpuid functions):",
          ["%x" % w for w in sorted(flag_words)])
    readers = collections.defaultdict(list)
    for label, ranges in all_units:
        for b, e in ranges:
            for ins in dis.linear(b, e):
                for o in ins.ops:
                    if o[0] == "m" and o[6] == "rip":
                        t = ins.rva + ins.size + o[4]
                        if t in flag_words:
                            readers[label].append((ins.rva, ins.mn, ins.text))
        dis.cache_clear()
    print("   units that read a flag word:", {k: len(v) for k, v in readers.items()} or "none")
    # 5. how the unreached ISA units could still be entered ----------------------
    print("\n5. Entry evidence for ISA units")
    isa_begins = {units.ranges[l][0][0]: l for l, _, _ in rows}
    dword_hits = collections.defaultdict(list)
    for sec in pe.sections:
        if pe.is_exec(sec) or not sec["rsize"]:
            continue
        blob = pe.data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
        for off in range(0, len(blob) - 3, 4):
            (v,) = struct.unpack_from("<I", blob, off)
            if v in isa_begins:
                dword_hits[isa_begins[v]].append(sec["va"] + off)
    print("   4-byte RVA entries in data that name an ISA unit (table candidates): %d units" % len(dword_hits))
    for l in list(dword_hits)[:12]:
        print("     %s: %d entries at %s" % (l, len(dword_hits[l]), " ".join("%x" % a for a in dword_hits[l][:4])))
    print("   ISA units referenced by an 8-byte relocation at their begin: %s" % (
        sorted(set(begins[t] for t in reloc_begin if begins[t] in fn_isa)) or "none"))
    print("   ISA units referenced by lea at their begin: %s" % (
        sorted(set(begins[t] for t in lea_begin if begins[t] in fn_isa)) or "none"))
    for t in sorted(lea_begin):
        if begins[t] in fn_isa:
            print("     lea into %s from %s" % (begins[t], [("%s@%x" % x) for x in lea_begin[t][:4]]))
    # forward reach from flag readers into ISA units
    def down(label, depth=8):
        seen, frontier = {label}, {label}
        for _ in range(depth):
            nxt = set()
            for x in frontier:
                for y in callees.get(x, ()):
                    if y not in seen:
                        seen.add(y)
                        nxt.add(y)
            frontier = nxt
        return seen
    isa_set = set(fn_isa)
    print("   flag-word readers and the ISA units they reach by direct calls (depth 8):")
    for r in sorted(readers):
        if r == "f51f8c0":
            continue
        reach_isa = sorted(down(r) & isa_set)
        print("     %s reads %d flag site(s); reaches %d ISA units %s" % (
            r, len(readers[r]), len(reach_isa), reach_isa[:5]))
    # 6. chains from roots into ISA units, and flag gating on each chain edge -------
    def chain_from_root(u):
        """Shortest caller chain from a root unit down to u (root first), or None."""
        from collections import deque
        prev = {u: None}
        dq = deque([u])
        while dq:
            x = dq.popleft()
            if x in root_units:
                # prev[c] = x means c calls x, so following prev from the root walks down to u
                chain = [x]
                while prev[chain[-1]] is not None:
                    chain.append(prev[chain[-1]])
                return chain
            for c in sorted(calls.get(x, ())):
                if c not in prev:
                    prev[c] = x
                    dq.append(c)
        return None

    def gated(caller, call_rva):
        """Is the call at call_rva in `caller` only reachable through a branch that reads a flag word?
        Returns the list of (branch rva, branch mnemonic, flag site, successor taken) for the gates."""
        ranges = units.ranges[caller]
        insns, succ, _ = descend(dis, ranges, [b for b, _ in ranges])
        if call_rva not in insns:
            return None
        pr = preds(succ)
        entry = ranges[0][0]
        out = []
        for b, ins in insns.items():
            if not ins.is_jcc or len(succ.get(b, [])) != 2:
                continue
            # the flag read: a flag-word operand within 8 instructions before the branch
            flag = None
            seen, frontier = set(), [b]
            for _ in range(8):
                nxt = []
                for x in frontier:
                    for pidx in pr.get(x, []):
                        if pidx in seen or pidx not in insns:
                            continue
                        seen.add(pidx)
                        ii = insns[pidx]
                        for o in ii.ops:
                            if o[0] == "m" and o[6] == "rip" and (ii.rva + ii.size + o[4]) in flag_words:
                                flag = (pidx, ii.rva + ii.size + o[4], ii.mn + " " + ii.text)
                        nxt.append(pidx)
                frontier = nxt
            if flag is None:
                continue
            for s2 in succ[b]:
                # reachability from entry with the edge b->s2 removed
                seen2, work = set(), [entry]
                while work:
                    x = work.pop()
                    if x in seen2 or x not in insns:
                        continue
                    seen2.add(x)
                    for y in succ.get(x, []):
                        if x == b and y == s2:
                            continue
                        work.append(y)
                if call_rva not in seen2:
                    out.append((b, ins.mn, flag[1], "taken" if s2 == ins.target else "fallthrough", flag[2]))
        return out

    print("\n6. Caller chains from roots into the ISA units, and flag gates on each edge")
    reachable_isa = [l for l, _, _ in rows if reach_info[l]]
    printed_roots = set()
    for label in reachable_isa:
        ch = chain_from_root(label)
        print("   unit %s (%s)" % (label, ",".join("%s:%d" % kv for kv in sorted(fn_isa[label].items()))))
        if not ch:
            print("     no chain found")
            continue
        root = ch[0]
        if root not in printed_roots:
            printed_roots.add(root)
            b0 = units.ranges[root][0][0]
            why = []
            if root in set(export_names) or any(units.index_of(v) == root for v in exports.values()):
                why.append("export")
            if units.index_of(pe.entry_rva) == root:
                why.append("entry point")
            if b0 in relocs:
                why.append("pointer in data at %s" % " ".join("%x" % x for x in relocs[b0][:3]))
            lea_here = lea_units.get(root, [])
            if lea_here:
                why.append("lea from code: " + " ".join("%s@%x" % x for x in lea_here[:3]))
                for cl, r in lea_here[:3]:
                    g = gated(cl, r)
                    why.append("  lea %s@%x gate: %s" % (cl, r, "none" if not g else "; ".join(
                        "branch %x %s flag %x [%s] reached %s" % (gb, gm, gf, gt, gs) for gb, gm, gf, gs, gt in g)))
            print("     ROOT %s: %s" % (root, "; ".join(why) or "unknown"))
        for a, c in zip(ch, ch[1:]):
            sites_ = [r for (cl, r) in call_sites.get(c, []) if cl == a]
            gates = []
            for r in sites_:
                g = gated(a, r)
                gates.append((r, g))
            desc = []
            for r, g in gates:
                if g:
                    desc.append("%x gated by %s" % (r, "; ".join("branch %x (%s, flag %x [%s], reached %s)" % (
                        gb, gm, gf, gt, gs) for gb, gm, gf, gs, gt in g)))
                elif g is None:
                    desc.append("%x (not in graph)" % r)
                else:
                    desc.append("%x ungated" % r)
            print("     %s -> %s : %s" % (a, c, " | ".join(desc) or "no call-site record"))

    # 7. code that reads the pointer tables holding the roots --------------------
    print("\n7. Code that reads the relocated pointer slots holding the chain roots")
    for root in sorted(printed_roots):
        b0 = units.ranges[root][0][0]
        for slot in relocs.get(b0, [])[:1]:
            refs = sorted({(u, r, m) for t, u, r, m in rip_refs if slot - 0x400 <= t <= slot + 0x400})
            print("   root %s slot %x: %d code reference(s) within 0x400 of the slot" % (root, slot, len(refs)))
            for u, r, m in refs[:8]:
                g = gated(u, r)
                fl = "ungated" if not g else "gated: " + "; ".join("branch %x %s flag %x [%s] %s" % (
                    gb, gm, gf, gt, gs) for gb, gm, gf, gs, gt in g)
                print("     %s@%x (%s)  %s" % (u, r, m, fl))
    # 8. global selectors: stores by the flag readers, loads by the table readers -------
    print("\n8. Globals written by flag readers and read by the table readers")
    table_readers = sorted({u for t, u, r, m in rip_refs})
    for R in sorted(readers):
        Ws = rip_stores.get(R, set())
        hits = []
        for D in table_readers:
            shared = Ws & rip_loads.get(D, set())
            if shared:
                hits.append((D, sorted("%x" % x for x in shared)[:3]))
        print("   flag reader %s stores %s; read by table readers: %s" % (
            R, sorted("%x" % x for x in Ws)[:4] or "none", hits[:6] or "none"))
    # 9. the selector block: gates on it, and the calls it guards ---------------
    selector = set()
    for R in readers:
        if R in ("f404740",):
            selector |= {t for t in rip_stores.get(R, set())}
    flag_words |= selector
    print("\n9. Selector words written from the cpuid flags: %s" % sorted("%x" % x for x in selector))
    for D in sorted({u for u in table_readers if rip_loads.get(u, set()) & selector}):
        refs = sorted({(r, m) for t, u, r, m in rip_refs if u == D})
        g_list = []
        for r, m in refs[:6]:
            g = gated(D, r)
            g_list.append("%x(%s):%s" % (r, m, "ungated" if not g else "; ".join(
                "branch %x %s flag %x" % (gb, gm, gf) for gb, gm, gf, gs, gt in g)))
        print("   table reader %s reads selector words; its table references: %s" % (D, " | ".join(g_list)))
    print("   calls from f404b40 into ISA units:", sorted({(u, r) for u in ["f404b40"] for r in []}))
    json_out = {"gap": {"gaps": gap["gaps"], "gap_bytes": gap["gap_bytes"], "isa": gap["isa"],
                        "where": gap["where"]},
                "isa_units": [{"unit": l, "bytes": sz, "groups": c} for l, sz, c in rows],
                "unreached_isa_units": [l for l, _, _ in rows if not reach_info[l]],
                "flag_words": ["%x" % w for w in sorted(flag_words)],
                "flag_readers": {k: [("%x" % a, m, t) for a, m, t in v] for k, v in readers.items()}}
    with open(os.path.join(os.path.dirname(sys.argv[1]), "q2_isa.json"), "w") as f:
        json.dump(json_out, f, indent=1)


if __name__ == "__main__":
    main()
