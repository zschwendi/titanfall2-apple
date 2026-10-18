#!/usr/bin/env python3
"""Question 3 and Question 4.

Q3. Every rdtsc/rdtscp site, grouped by enclosing function. For each function the TSC value
    (edx:eax after rdtsc) is followed forward over the function's CFG, through registers and
    memory slots, and its sinks are recorded: returned (ret), stored to memory, subtracted
    (a delta), mixed by xor/imul/rotate/shift (hashing), or passed to a call. The sinks give
    a purpose: timing helper, elapsed-time delta, seed or hash, or other.

Q4. stmxcsr/ldmxcsr sites. For each stmxcsr, the stored image is followed through its memory
    slot: the immediates applied to it (and/or/xor/bts/btr), and whether it is written back.
    For each ldmxcsr, the source is classified (constant stored just before it in the same
    function, a stack slot, or a global) and the constant is decoded into MXCSR bits.
    Scope: a function that stmxcsr-saves and later ldmxcsr-restores the same slot is scoped;
    a load from a global, or an ldmxcsr with no matching save, is a persistent change.

    .venv/bin/python tools/scout/cpu_q3_q4.py <scratchpad>/sites.json game
"""

import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import Dis, descend, function_table, game_images, load, preds  # noqa: E402
from cpu_q1_cpuid import mem_key  # noqa: E402

MXCSR_BITS = {0: "IE", 1: "DE", 2: "ZE", 3: "OE", 4: "UE", 5: "PE", 6: "DAZ", 7: "IM", 8: "DM", 9: "ZM",
              10: "OM", 11: "UM", 12: "PM", 13: "RC0", 14: "RC1", 15: "FZ"}


def mxcsr_names(v):
    names = [MXCSR_BITS[b] for b in range(16) if v >> b & 1]
    rc = (v >> 13) & 3
    return "0x%04x [%s] rounding=%s" % (v & 0xFFFF, " ".join(names), ["nearest", "down", "up", "zero"][rc])


def walk(site_rva, succ, insns, init_regs, init_mem, budget=800, on_use=None, on_store=None):
    """Generic forward taint walk. init_regs: {family: origin}; init_mem: {slot: origin}.
    on_use(origin, ins, reg_or_slot) and on_store(origin, ins, slot) are called for each hit."""
    work = [(s, dict(init_regs), dict(init_mem)) for s in succ.get(site_rva, [])]
    seen = set()
    steps = 0
    while work and steps < budget:
        node, regs, mem = work.pop()
        key = (node, tuple(sorted(regs.items())), tuple(sorted(mem.items(), key=str)))
        if key in seen:
            continue
        seen.add(key)
        steps += 1
        ins = insns.get(node)
        if ins is None or (not regs and not mem):
            continue
        for o in ins.ops:
            if o[0] == "r" and o[1] in regs and o[1] in ins.rd and on_use:
                on_use(regs[o[1]], ins, o[2])
            if o[0] == "m" and mem_key(ins, o) in mem and on_use:
                on_use(mem[mem_key(ins, o)], ins, "[slot]")
        if ins.is_ret:
            if on_use:
                on_use(regs.get("a"), ins, "ret") if "a" in regs else None
            continue
        new_regs, new_mem = dict(regs), dict(mem)
        if ins.mn.startswith("mov") and len(ins.ops) == 2 and ins.ops[0][0] == "m":
            k = mem_key(ins, ins.ops[0])
            if ins.ops[1][0] == "r" and ins.ops[1][1] in regs:
                new_mem[k] = regs[ins.ops[1][1]]
                if on_store:
                    on_store(regs[ins.ops[1][1]], ins, k)
            else:
                new_mem.pop(k, None)
        elif ins.mn.startswith("mov") and len(ins.ops) == 2 and ins.ops[0][0] == "r" and ins.ops[1][0] == "m":
            k = mem_key(ins, ins.ops[1])
            new_regs.pop(ins.ops[0][1], None)
            if k in mem:
                new_regs[ins.ops[0][1]] = mem[k]
        else:
            src = next((regs[f] for f in sorted(ins.rd) if f in regs), None)
            for w in ins.wr:
                new_regs.pop(w, None)
            if src is not None:
                for w in ins.wr:
                    new_regs[w] = src
        if ins.mn == "call":
            new_regs = {k: v for k, v in new_regs.items() if k not in ("a", "c", "d", "r8", "r9", "r10", "r11")}
        for s2 in succ.get(node, []):
            work.append((s2, new_regs, new_mem))
    return steps


def tsc_purpose(sinks):
    if "ret" in sinks:
        return "timing helper (returns the TSC)"
    if "sub" in sinks:
        return "elapsed-time delta (profiler/timer scope)"
    if "mix" in sinks:
        return "seed or hash (mixed, then used)"
    if "store" in sinks:
        return "timestamp stored to memory"
    if "call" in sinks:
        return "passed to a callee"
    return "other / unused result"


def analyse_rdtsc(dis, funcs, sites):
    out = []
    by_func = collections.defaultdict(list)
    for s in sites:
        by_func[s["func"]].append(s)
    for func, ss in by_func.items():
        ranges = funcs.get(func, [(func, func + 1)])
        insns, succ, unresolved = descend(dis, ranges, [b for b, _ in ranges])
        size = sum(e - b for b, e in ranges)
        for s in ss:
            rva = s["rva"]
            if rva not in insns:
                out.append({"rva": rva, "func": func, "reached": False})
                continue
            sinks = set()
            mix_ops = collections.Counter()

            def on_use(origin, ins, what):
                if ins.mn in ("sub", "subsd", "subss"):
                    sinks.add("sub")
                elif ins.mn in ("xor", "imul", "rol", "ror", "mul", "shld", "shrd", "pmul"):
                    sinks.add("mix")
                    mix_ops[ins.mn] += 1
                elif ins.mn == "call":
                    sinks.add("call")
                elif ins.mn.startswith("ret"):
                    sinks.add("ret")

            def on_store(origin, ins, slot):
                sinks.add("store")

            # rdtsc writes eax and edx; the combined value is what flows on
            walk(rva, succ, insns, {"a": "tsc", "d": "tsc"}, {}, on_use=on_use, on_store=on_store)
            n_rdtsc = sum(1 for i in insns.values() if i.mn in ("rdtsc", "rdtscp"))
            out.append({"rva": rva, "func": func, "reached": True, "size": size, "insns": len(insns),
                        "rdtsc_in_func": n_rdtsc, "sinks": sorted(sinks), "mix": dict(mix_ops),
                        "purpose": tsc_purpose(sinks), "unresolved": len(unresolved)})
        dis.cache_clear()
    return out


def analyse_mxcsr(dis, funcs, sites):
    out = []
    by_func = collections.defaultdict(list)
    for s in sites:
        if s["mn"] in ("stmxcsr", "ldmxcsr"):
            by_func[s["func"]].append(s)
    for func, ss in by_func.items():
        ranges = funcs.get(func, [(func, func + 1)])
        insns, succ, unresolved = descend(dis, ranges, [b for b, _ in ranges])
        pr = preds(succ)
        saved_slots = set()
        for s in ss:
            rva = s["rva"]
            if rva not in insns:
                out.append({"rva": rva, "func": func, "mn": s["mn"], "reached": False})
                continue
            ins = insns[rva]
            mop = next((o for o in ins.ops if o[0] == "m"), None)
            slot = mem_key(ins, mop) if mop else None
            rec = {"rva": rva, "func": func, "mn": s["mn"], "reached": True, "text": ins.text,
                   "unresolved": len(unresolved), "slot": str(slot)}
            if s["mn"] == "stmxcsr":
                saved_slots.add(slot)
                ands, ors, stores = [], [], []

                def on_use(origin, ins2, what):
                    if ins2.mn in ("and", "or", "xor", "bts", "btr", "btc", "btr") and ins2.ops:
                        imm = [o[1] for o in ins2.ops if o[0] == "i"]
                        if imm:
                            (ands if ins2.mn == "and" else ors).append((ins2.mn, hex(imm[0] & 0xFFFFFFFF)))

                def on_store(origin, ins2, k):
                    stores.append(str(k))

                walk(rva, succ, insns, {}, {slot: "mx"}, on_use=on_use, on_store=on_store)
                rec.update(ops_on_image=sorted(set(ands + ors)), written_back_to=sorted(set(stores)))
                rec["scope_hint"] = "stack save" if slot and slot[0] in ("sp", "bp") else (
                    "global" if slot and slot[0] == "rip" else "other")
            else:
                # ldmxcsr source: a slot. Find a constant stored to that slot on the way in.
                const = set()
                seen, stack = set(), list(pr.get(rva, []))
                while stack:
                    n = stack.pop()
                    if n in seen or n not in insns:
                        continue
                    seen.add(n)
                    x = insns[n]
                    if x.mn == "mov" and len(x.ops) == 2 and x.ops[0][0] == "m" and x.ops[1][0] == "i" \
                            and mem_key(x, x.ops[0]) == slot:
                        const.add(x.ops[1][1] & 0xFFFFFFFF)
                        continue
                    if x.mn.startswith("mov") and len(x.ops) == 2 and x.ops[0][0] == "m" and mem_key(x, x.ops[0]) == slot:
                        const.add("non-constant store")
                        continue
                    stack.extend(pr.get(n, []))
                rec["source"] = ("stack slot" if slot and slot[0] in ("sp", "bp") else
                                 "global" if slot and slot[0] == "rip" else "other")
                rec["constants_before"] = sorted(str(c) if isinstance(c, str) else mxcsr_names(c) for c in const)
                rec["restores_saved_slot_in_func"] = slot in saved_slots
            out.append(rec)
        dis.cache_clear()
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    data = json.load(open(sys.argv[1]))
    paths = {os.path.basename(p): p for p in game_images(sys.argv[2])}
    rdtsc_rows, mx_rows = [], []
    for img in sorted(paths):
        sites = [s for s in data["sites"] if s["img"] == img]
        if not any(s["mn"] in ("rdtsc", "rdtscp", "stmxcsr", "ldmxcsr") for s in sites):
            continue
        pe = load(paths[img])
        entries, funcs = function_table(pe)
        dis = Dis(pe)
        r_sites = [s for s in sites if s["mn"] in ("rdtsc", "rdtscp")]
        for row in analyse_rdtsc(dis, funcs, r_sites):
            row["img"] = img
            rdtsc_rows.append(row)
        m_sites = [s for s in sites if s["mn"] in ("stmxcsr", "ldmxcsr")]
        for row in analyse_mxcsr(dis, funcs, m_sites):
            row["img"] = img
            mx_rows.append(row)
    out_dir = os.path.dirname(sys.argv[1])
    json.dump({"rdtsc": rdtsc_rows, "mxcsr": mx_rows}, open(os.path.join(out_dir, "q3_q4.json"), "w"),
              indent=1, default=str)

    # Q3 report
    print("Q3. rdtsc/rdtscp sites: %d in %d distinct functions (%d reached by recursive descent)" % (
        len(rdtsc_rows), len({(r["img"], r["func"]) for r in rdtsc_rows}),
        sum(1 for r in rdtsc_rows if r.get("reached"))))
    by = collections.defaultdict(list)
    for r in rdtsc_rows:
        by[(r["img"], r["func"])].append(r)
    print("%-22s %-9s %-6s %-6s %-40s %s" % ("image", "function", "size", "rdtsc", "purpose", "sinks"))
    for (img, f), rows in sorted(by.items()):
        r0 = rows[0]
        sinks = sorted({k for r in rows for k in r.get("sinks", [])})
        purpose = collections.Counter(r.get("purpose", "unreached") for r in rows).most_common(1)[0][0]
        print("%-22s %-9x %-6s %-6d %-40s %s" % (img, f, r0.get("size", "-"), len(rows), purpose,
                                                 ",".join(sinks) or "-"))
    cnt = collections.Counter(r.get("purpose") for r in rdtsc_rows)
    print("\nQ3 by purpose (sites):", dict(cnt.most_common()))
    print("Q3 functions by purpose:", dict(collections.Counter(
        collections.Counter(r.get("purpose", "unreached") for r in rows).most_common(1)[0][0]
        for rows in by.values())))

    # Q4 report
    print("\nQ4. stmxcsr/ldmxcsr: %d stmxcsr, %d ldmxcsr in %d functions" % (
        sum(1 for r in mx_rows if r["mn"] == "stmxcsr"), sum(1 for r in mx_rows if r["mn"] == "ldmxcsr"),
        len({(r["img"], r["func"]) for r in mx_rows})))
    for r in mx_rows:
        if r["mn"] == "stmxcsr":
            print("  stmxcsr %s@%x f%x slot=%s scope=%s ops=%s writeback=%s" % (
                r["img"], r["rva"], r["func"], r.get("slot"), r.get("scope_hint"),
                r.get("ops_on_image"), r.get("written_back_to")))
        else:
            print("  ldmxcsr %s@%x f%x source=%s const=%s restores_saved=%s" % (
                r["img"], r["rva"], r["func"], r.get("source"), r.get("constants_before"),
                r.get("restores_saved_slot_in_func")))


if __name__ == "__main__":
    main()
