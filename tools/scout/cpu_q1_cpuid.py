#!/usr/bin/env python3
"""Question 1: every cpuid site, its enclosing function, the leaf and sub-leaf it asks for,
and the result bits the code then tests.

For each site (from sites.json, written by cpu_sites.py):
  * the function CFG comes from recursive descent over the function's .pdata ranges;
  * leaf (eax) and sub-leaf (ecx) are found by backward reaching definitions over that CFG:
    a constant `mov reg, imm` / `xor reg, reg` on every path gives a value; a value that
    arrives from the caller is reported as 'param'; anything computed is 'computed';
  * the four result registers are then followed forward over the CFG. Each use of a
    result register with an immediate (test, and, bt, cmp, shr, ...) is recorded with the
    register it came from, so bit masks can be named. Stores of a result register to memory
    are recorded as well (vendor and brand strings are stored, not tested).

Prints a per-site table and a grouped summary. Nothing is written to the repo.

    .venv/bin/python tools/scout/cpu_q1_cpuid.py <scratchpad>/sites.json game
"""

import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu_core import Dis, descend, function_table, in_mask, load, preds  # noqa: E402

VOLATILE = {"a", "c", "d", "r8", "r9", "r10", "r11"}
TEST_MN = ("test", "and", "cmp", "bt", "btc", "bts", "btr")
ROOT_SET = {"b", "c", "d", "a"}

# Named bits, keyed by (leaf, subleaf, output register) -> {bit: name}.
FEATURES = {
    (0x1, None, "d"): {0: "FPU", 4: "TSC", 5: "MSR", 8: "CX8", 9: "APIC", 11: "SEP", 15: "CMOV",
                       19: "CLFSH", 21: "DS", 23: "MMX", 24: "FXSR", 25: "SSE", 26: "SSE2", 28: "HTT"},
    (0x1, None, "c"): {0: "SSE3", 1: "PCLMULQDQ", 9: "SSSE3", 12: "FMA", 13: "CMPXCHG16B", 19: "SSE4.1",
                       20: "SSE4.2", 22: "MOVBE", 23: "POPCNT", 25: "AESNI", 26: "XSAVE",
                       27: "OSXSAVE", 28: "AVX", 29: "F16C", 30: "RDRAND", 31: "hypervisor"},
    (0x7, 0, "b"): {0: "FSGSBASE", 3: "BMI1", 5: "AVX2", 8: "BMI2", 9: "ERMS", 11: "RTM", 16: "AVX512F",
                    18: "RDSEED", 19: "ADX", 20: "SMAP", 23: "CLFLUSHOPT", 29: "SHA"},
    (0x7, 0, "c"): {1: "AVX512VBMI", 2: "UMIP", 3: "PKU", 8: "GFNI", 9: "VAES", 10: "VPCLMULQDQ"},
    (0x7, 0, "d"): {2: "AVX512_4VNNIW", 3: "AVX512_4FMAPS", 26: "IBRS/IBPB", 29: "ARCH_CAPABILITIES"},
    (0xD, 0, "a"): {0: "XCR0.x87", 1: "XCR0.SSE", 2: "XCR0.AVX", 5: "XCR0.opmask", 6: "XCR0.ZMM_hi256",
                    7: "XCR0.hi16_ZMM"},
    (0xD, 1, "a"): {0: "XSAVEOPT", 1: "XSAVEC", 2: "XGETBV_ECX1", 3: "XSAVES"},
    (0x80000001, None, "c"): {0: "LAHF64", 5: "LZCNT", 6: "SSE4A", 8: "PREFETCHW", 11: "XOP", 16: "FMA4"},
    (0x80000001, None, "d"): {11: "SYSCALL", 20: "NX", 26: "1GB_pages", 27: "RDTSCP", 29: "LM(x64)"},
    (0x80000007, None, "d"): {8: "invariant_TSC"},
    (0x80000008, None, "b"): {0: "WBNOINVD", 9: "WBNOINVD2"},
}
# A test against a whole result register named by leaf: used when the bit map is absent.
LEAF_MEANING = {
    0x0: "vendor string / max basic leaf",
    0x1: "family/model/stepping, feature bits, logical CPU count (EBX[23:16])",
    0x2: "legacy cache descriptors",
    0x4: "deterministic cache parameters",
    0x6: "thermal and power management",
    0xB: "extended topology (SMT/core level)",
    0x15: "TSC/core crystal clock ratio",
    0x16: "processor base/max frequency (MHz)",
    0x1F: "V2 extended topology",
    0x7: "structured extended features",
    0xD: "XSAVE state size and XCR0 support",
    0x80000000: "max extended leaf",
    0x80000001: "extended feature bits (LZCNT, XOP, RDTSCP, SYSCALL, ...)",
    0x80000002: "brand string [0:15]",
    0x80000003: "brand string [16:31]",
    0x80000004: "brand string [32:47]",
    0x80000005: "L1 cache/TLB descriptors",
    0x80000006: "L2/L3 cache descriptors",
    0x80000007: "power management (invariant TSC)",
    0x80000008: "address sizes, core count (ECX[7:0])",
    0x8000001D: "AMD cache topology",
    0x8000001E: "AMD core/APIC topology",
}


class ConstEval:
    """Backward constant evaluation of a register over one function's CFG.

    values_before(reg, at) gives the possible values of a register family on entry to
    instruction `at`: ints for constants, 'param' for a value that arrives from the caller,
    and labels for anything computed from memory or from a call. Follows mov reg,reg;
    lea reg,[base+disp]; add/sub reg,imm; and xor reg,reg. Depth-limited.
    """

    def __init__(self, insns, pred):
        self.insns = insns
        self.pred = pred
        self.memo = {}

    def values_before(self, reg, at, depth=0):
        key = (reg, at)
        if key in self.memo:
            return self.memo[key]
        out = set()
        if depth > 8:
            return {"too_deep"}
        self.memo[key] = {"cycle"}  # guards loops
        ps = self.pred.get(at, [])
        if not ps:
            out.add("param:" + reg)
        seen = set()
        stack = list(ps)
        while stack:
            n = stack.pop()
            if n in seen:
                continue
            seen.add(n)
            ins = self.insns.get(n)
            if ins is None:
                out.add("undecoded")
                continue
            if reg in ins.wr:
                out |= self.value_after(ins, reg, depth)
                continue
            pn = self.pred.get(n, [])
            if not pn:
                out.add("param:" + reg)
            stack.extend(pn)
        self.memo[key] = out
        return out

    def value_after(self, ins, reg, depth):
        ops = ins.ops
        if ins.mn == "mov" and len(ops) == 2 and ops[0][0] == "r":
            if ops[1][0] == "i":
                return {in_mask(ops[1][1], ops[0][3])}
            if ops[1][0] == "r":
                return {v for v in self.values_before(ops[1][1], ins.rva, depth + 1)}
            return {"load:" + ins.text}
        if ins.mn == "xor" and len(ops) == 2 and ops[0][0] == "r" and ops[1][0] == "r" \
                and ops[0][2] == ops[1][2]:
            return {0}
        if ins.mn == "lea" and len(ops) == 2 and ops[1][0] == "m" and ops[1][2] is None and ops[1][6] != "rip":
            disp = ops[1][4]
            if ops[1][1] is None:
                return {in_mask(disp, ops[0][3])}
            base_vals = self.values_before(ops[1][1], ins.rva, depth + 1)
            return {v + disp if isinstance(v, int) else v for v in base_vals} if True else set()
        if ins.mn in ("add", "sub") and len(ops) == 2 and ops[0][0] == "r" and ops[1][0] == "i":
            vals = self.values_before(reg, ins.rva, depth + 1)
            k = ops[1][1] if ins.mn == "add" else -ops[1][1]
            return {in_mask(v + k, ops[0][3]) if isinstance(v, int) else v for v in vals}
        if ins.mn == "cpuid":
            return {"cpuid_output"}
        if ins.mn == "call":
            return {"clobbered_by_call"}
        if ins.mn == "lea":
            return {"lea:" + ins.text}
        return {"computed:" + ins.mn}


def reach_value(reg, at, insns, pred, ce=None):
    ce = ce or ConstEval(insns, pred)
    return ce.values_before(reg, at)


def mem_key(ins, op):
    """Slot identity of a memory operand: (base family, index family, disp), or the absolute
    address for RIP-relative operands."""
    if op[6] == "rip":
        return ("rip", ins.rva + ins.size + op[4])
    return (op[1], op[2], op[4])


def forward_uses(site_rva, insns, succ, budget=600):
    """Follow the four cpuid output registers forward over the CFG, through registers and
    through memory slots (a result stored to [rbp-0x50] and reloaded keeps its origin).
    Returns (uses, stores).
      uses:   set of (origin_reg, mnemonic, operand text, immediate text or None)
      stores: set of (origin_reg, memory text) for result registers written to memory"""
    uses, stores = set(), set()
    start_regs = {"a": "a", "b": "b", "c": "c", "d": "d"}
    work = [(s, start_regs, {}) for s in succ.get(site_rva, [])]
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
        imm = [o[1] for o in ins.ops if o[0] == "i"]
        imm_txt = None
        if imm:
            imm_txt = ("bit %d" % imm[0]) if ins.mn.startswith("bt") else hex(imm[0] & 0xFFFFFFFFFFFFFFFF)
        # 1. tests on a tainted register operand
        hit = [o for o in ins.ops if o[0] == "r" and o[1] in regs and o[1] in ins.rd]
        if hit:
            uses.add((regs[hit[0][1]], ins.mn, hit[0][2], imm_txt))
        # 2. tests on a tainted memory slot
        for o in ins.ops:
            if o[0] == "m":
                k = mem_key(ins, o)
                if k in mem and ins.mn.startswith(TEST_MN) and ins.mn not in ("cmp",) or (
                        k in mem and ins.mn == "cmp"):
                    uses.add((mem[k], ins.mn, "[slot]", imm_txt))
        # 3. stores of result registers to memory
        if ins.mn.startswith("mov") and len(ins.ops) == 2 and ins.ops[0][0] == "m" \
                and ins.ops[1][0] == "r" and ins.ops[1][1] in regs:
            m = ins.ops[0]
            stores.add((regs[ins.ops[1][1]], "[%s%+d]" % (m[6] or "?", m[4])))
        if ins.is_ret:
            continue
        new_regs = dict(regs)
        new_mem = dict(mem)
        if ins.mn.startswith("mov") and len(ins.ops) == 2 and ins.ops[0][0] == "m":
            k = mem_key(ins, ins.ops[0])
            if ins.ops[1][0] == "r" and ins.ops[1][1] in regs:
                new_mem[k] = regs[ins.ops[1][1]]
            else:
                new_mem.pop(k, None)
        elif ins.mn.startswith("mov") and len(ins.ops) == 2 and ins.ops[0][0] == "r" and ins.ops[1][0] == "m":
            k = mem_key(ins, ins.ops[1])
            new_regs.pop(ins.ops[0][1], None)
            if k in mem:
                new_regs[ins.ops[0][1]] = mem[k]
        else:
            src_origin = next((regs[f] for f in sorted(ins.rd) if f in regs), None)
            for w in ins.wr:
                new_regs.pop(w, None)
            if src_origin is not None:
                for w in ins.wr:
                    new_regs[w] = src_origin
            for o in ins.ops:
                if o[0] == "m" and ins.mn not in TEST_MN and ins.ops[0][0] == "m":
                    new_mem.pop(mem_key(ins, o), None)
        if ins.mn == "call":
            new_regs = {k: v for k, v in new_regs.items() if k not in VOLATILE}
        for s2 in succ.get(node, []):
            work.append((s2, new_regs, new_mem))
    return uses, stores




def name_bits(leaf, sub, origin, imm, mn):
    """Turn a test-like use's immediate into bit names for the known (leaf, sub, register) map.
    Arithmetic (add/sub) and shifts are not bit tests and are not named."""
    table = FEATURES.get((leaf, sub, origin)) or FEATURES.get((leaf, None, origin))
    if not table or imm is None or not mn.startswith(TEST_MN):
        return []
    if imm.startswith("bit "):
        b = int(imm.split()[1])
        return [table.get(b, "bit%d" % b)]
    v = int(imm, 16)
    names = [name for bit, name in sorted(table.items()) if v >> bit & 1]
    return names


def single(vals):
    """One constant if the value set is exactly one constant; otherwise the sorted labels."""
    if len(vals) == 1:
        (v,) = vals
        return v
    return "{" + ", ".join(sorted(map(str, vals))) + "}"


def purpose(leaf, sub, used, stored):
    if leaf is None:
        return "unresolved leaf"
    if isinstance(leaf, str):
        return "leaf not constant (%s)" % ("from caller" if "param" in leaf else "computed")
    if leaf in (0, 0x80000000):
        return "vendor string" if (stored and not used) else "max leaf / vendor check"
    if leaf in (0x80000002, 0x80000003, 0x80000004):
        return "brand string"
    if leaf in (0x1, 0x7, 0xD, 0x80000001, 0x80000007, 0x80000008):
        return "feature detection"
    if leaf in (0x4, 0xB, 0x1F, 0x2, 0x80000005, 0x80000006, 0x8000001D, 0x8000001E):
        return "core/cache topology"
    if leaf in (0x15, 0x16, 0x6):
        return "timing calibration / clock" if leaf != 0x6 else "power/thermal (feature detection)"
    return "other"



def call_sites(pe, helpers, funcs):
    """Direct calls in the image whose target is one of `helpers` (function keys).
    Returns {helper: [(caller_key, call_rva), ...]}."""
    dis = Dis(pe)
    found = collections.defaultdict(list)
    targets = set(helpers)
    for key, ranges in funcs.items():
        for b, e in ranges:
            for ins in dis.linear(b, e):
                if ins.mn == "call" and ins.target in targets:
                    found[ins.target].append((key, ins.rva))
    return found


def resolve_callers(path, recs, funcs):
    """For helper functions whose leaf or sub-leaf arrives in a register, evaluate that
    register at every direct call site in the image and record the constants."""
    helper_keys = set()
    for r in recs:
        if not r.get("reached"):
            continue
        for field in ("leaf_vals", "sub_vals"):
            for v in r.get(field, []):
                if v.startswith("param:"):
                    helper_keys.add(r["func"])
    if not helper_keys:
        return {}
    pe = load(path)
    sites = call_sites(pe, helper_keys, funcs)
    dis = Dis(pe)
    per_caller = {}
    out = {}
    for helper in helper_keys:
        rows = []
        for caller, call_rva in sites.get(helper, []):
            if caller not in per_caller:
                ranges = funcs[caller]
                insns, succ, _ = descend(dis, ranges, [b for b, _ in ranges])
                per_caller[caller] = (insns, ConstEval(insns, preds(succ)))
            insns, ce = per_caller[caller]
            if call_rva not in insns:
                rows.append({"caller": "%x" % caller, "call": "%x" % call_rva, "unreached": True})
                continue
            # the helper's own entry tags name the registers that carry leaf and sub-leaf
            entry_tags = {}
            for r in recs:
                if r["func"] == helper:
                    for field, reg in (("leaf_vals", "a"), ("sub_vals", "c")):
                        for v in r.get(field, []):
                            if v.startswith("param:"):
                                entry_tags[reg] = v.split(":")[1]
            # the helper's leaf register is what the caller must set; default to ecx for sub-leaf
            row = {"caller": "%x" % caller, "call": "%x" % call_rva}
            for reg_in_helper, fam in entry_tags.items():
                vals = ce.values_before(fam, call_rva)
                row["arg_" + reg_in_helper] = sorted(v if isinstance(v, int) else str(v) for v in vals)
            rows.append(row)
        out[helper] = rows
    return out


def analyse(job):
    """Analyse all cpuid sites of one image. Returns a list of site records."""
    path, sites = job
    pe = load(path)
    funcs_entries, funcs = function_table(pe)
    dis = Dis(pe)
    by_func = collections.defaultdict(list)
    for s in sites:
        by_func[s["func"]].append(s)
    out = []
    for func, ss in by_func.items():
        ranges = funcs.get(func, [(func, func + 1)])
        insns, succ, unresolved = descend(dis, ranges, [b for b, _ in ranges])
        pred = preds(succ)
        for s in ss:
            rva = s["rva"]
            ok = rva in insns
            rec = {"img": s["img"], "rva": rva, "func": func, "reached": ok,
                   "unresolved_in_func": len(unresolved)}
            if not ok:
                out.append(rec)
                continue
            ce = ConstEval(insns, pred)
            leaf_vals = ce.values_before("a", rva)
            sub_vals = ce.values_before("c", rva)
            rec["leaf"] = single(leaf_vals)
            rec["sub"] = single(sub_vals)
            rec["leaf_vals"] = sorted(map(str, leaf_vals))
            rec["sub_vals"] = sorted(map(str, sub_vals))
            uses, stores = forward_uses(rva, insns, succ)
            rec["uses"] = sorted(uses, key=str)
            rec["stores"] = sorted(stores, key=str)
            lead = rec["leaf"] if isinstance(rec["leaf"], int) else None
            subl = rec["sub"] if isinstance(rec["sub"], int) else None
            uses = rec["uses"]
            named = set()
            for origin, mn, regname, imm in uses:
                for nm in name_bits(lead, subl, origin, imm, mn):
                    named.add("%s.%s:%s" % (origin, mn, nm))
            rec["named_bits"] = sorted(named)
            rec["purpose"] = purpose(lead if lead is not None else rec["leaf"], subl,
                                     bool(uses), bool(rec["stores"]))
            out.append(rec)
        dis.cache_clear()
    callers = resolve_callers(path, out, funcs)
    for r in out:
        if r["func"] in callers:
            r["callers"] = callers[r["func"]]
            union = collections.defaultdict(set)
            for row in r["callers"]:
                for k, v in row.items():
                    if k.startswith("arg_"):
                        union[k].update(v)
            r["caller_args"] = {k: sorted(v) for k, v in union.items()}
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sites_json, root = sys.argv[1], sys.argv[2]
    data = json.load(open(sites_json))
    from cpu_core import game_images
    paths = {os.path.basename(p): p for p in game_images(root)}
    per_img = collections.defaultdict(list)
    for s in data["sites"]:
        if s["mn"] == "cpuid":
            per_img[s["img"]].append(s)
    results = []
    for img in sorted(per_img):
        results.extend(analyse((paths[img], per_img[img])))
    out_path = os.path.join(os.path.dirname(sites_json), "q1_cpuid.json")
    with open(out_path, "w") as f:
        json.dump(results, f, indent=1, default=str)

    print("cpuid sites: %d, reached by recursive descent: %d" % (len(results), sum(r["reached"] for r in results)))
    print("\n%-22s %-10s %-8s %-10s %-10s %-34s %s" % ("image", "func", "rva", "leaf", "subleaf",
                                                   "purpose", "bits named / stores"))
    for r in results:
        if not r["reached"]:
            print("%-22s %-10s %-8x NOT REACHED" % (r["img"], "%x" % r["func"], r["rva"]))
            continue
        leaf = r["leaf"]
        leaf_s = ("%#x" % leaf) if isinstance(leaf, int) else str(leaf)
        sub = r["sub"]
        sub_s = ("%#x" % sub) if isinstance(sub, int) else ("-" if sub == "" else str(sub))
        extra = ""
        if r.get("caller_args"):
            extra = "  callers pass: " + " ".join("%s=%s" % (k, ",".join(str(x) for x in v))
                                                 for k, v in sorted(r["caller_args"].items()))
        print("%-22s %-10s %-8x %-10s %-10s %-34s %s | stores=%d%s" % (
            r["img"], "%x" % r["func"], r["rva"], leaf_s, sub_s, r["purpose"],
            "; ".join(r["named_bits"][:6]) or "-", len(r["stores"]), extra))

    print("\nBy purpose:")
    cnt = collections.Counter(r.get("purpose", "NOT REACHED") for r in results)
    for k, v in cnt.most_common():
        print("  %-36s %d" % (k, v))
    print("\nBy leaf:")
    lc = collections.Counter(("%#x" % r["leaf"]) if isinstance(r.get("leaf"), int) else str(r.get("leaf"))
                             for r in results)
    for k, v in sorted(lc.items(), key=lambda kv: kv[0]):
        print("  %-12s %d" % (k, v))


if __name__ == "__main__":
    main()
