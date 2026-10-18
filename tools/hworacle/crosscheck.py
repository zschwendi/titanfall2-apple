#!/usr/bin/env python3
"""Cross-check of the harness's emulator oracle against a real CPU.

    .venv/bin/python tools/hworacle/crosscheck.py insn --module tier0
    .venv/bin/python tools/hworacle/crosscheck.py insn --module corpus_O1 --dll tests/corpus/build/corpus_O1.dll
    .venv/bin/python tools/hworacle/crosscheck.py insn --module vectors      (tests/vectors.py's hand-written ones)
    .venv/bin/python tools/hworacle/crosscheck.py float
    .venv/bin/python tools/hworacle/crosscheck.py cases      one hand-written case per class of disagreement
    .venv/bin/python tools/hworacle/crosscheck.py report

`insn` builds single-instruction cases exactly as harness/difftest.py's instruction test does
(same sampling of the lifter's operand shapes, same insn_state, same memory images, same
Unicorn Oracle class, all imported and used as they are), runs every case in Unicorn and on the
hardware, and lists every disagreement grouped by mnemonic and by the part of the state that
differs.  `float` does the same for the SSE float operations of experiments/afp over its boundary
inputs and MXCSR settings, with its own one-instruction Unicorn runs.  `report` prints the merged
tables from the JSON files the other two leave under build/hworacle/crosscheck/.

Nothing is compared less strictly to make a case agree.  A disagreement is first labelled
mechanically: `undefined` (every differing flag or register is one the Intel SDM leaves undefined
for that instruction and input - the same rule the harness uses, difftest.undefined_after),
`known:cmpxchg32` (the difference disappears under difftest.oracle_corrections), or `open`.
Sorting the open classes into emulator bug, harness set-up difference and runner bug is done by
hand and recorded in docs/handoff/hw-oracle.md.

Answers come from the cache when they are there; add --offline to forbid ssh altogether.
"""

import argparse
import json
import os
import random
import struct
import sys
import time
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
for p in (REPO, os.path.join(REPO, "harness"), os.path.join(REPO, "experiments", "afp"), HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

import capstone                                       # noqa: E402
from capstone import x86_const as X                   # noqa: E402

import client as C                                    # noqa: E402

OUT = os.path.join(REPO, "build", "hworacle", "crosscheck")
REG_NAMES = "rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()
FLAG_BITS = {"cf": 0, "pf": 2, "af": 4, "zf": 6, "sf": 7, "df": 10, "of": 11}
EXC = {0: "#DE", 1: "#DB", 3: "int3", 6: "#UD", 12: "#SS", 13: "#GP", 14: "#PF", 17: "#AC", 19: "#XM"}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


# ---- what each side says, in common terms ---------------------------------------------------------

def hw_fault_kind(r):
    """(kind, address or None) of a hardware fault."""
    if r.trapno == 14:
        how = "write" if r.err & 2 else "fetch" if r.err & 16 else "read"
        return "%s-%s" % (how, "unmapped" if r.sig_code == 1 else "protected"), r.sig_addr
    return EXC.get(r.trapno, "trap %d" % r.trapno), None


def emu_fault_kind(reason, address):
    """The harness oracle's 'inconclusive' reason in the same terms."""
    for tag, kind in (("READ_UNMAPPED", "read-unmapped"), ("WRITE_UNMAPPED", "write-unmapped"),
                      ("FETCH_UNMAPPED", "fetch-unmapped"), ("READ_PROT", "read-protected"),
                      ("WRITE_PROT", "write-protected"), ("FETCH_PROT", "fetch-protected"), ("INSN_INVALID", "#UD")):
        if tag in reason:
            return kind, address
    if reason.startswith("fault: CPU exception "):
        n = int(reason.split()[3])
        return EXC.get(n, "exception %d" % n), None
    return "harness: " + reason.split(" at image+")[0], None


def changed_bytes(now, before, base):
    """{address: byte} where two equally long byte strings differ."""
    x = int.from_bytes(now, "little") ^ int.from_bytes(before, "little")
    if not x:
        return {}
    lo, hi = ((x & -x).bit_length() - 1) >> 3, (x.bit_length() - 1) >> 3
    return {base + i: now[i] for i in range(lo, hi + 1) if now[i] != before[i]}


def runs(d):
    """{address: byte} as a short text: address:hex bytes, contiguous runs."""
    out, cur = [], None
    for a in sorted(d):
        if cur and a == cur[0] + len(cur[1]):
            cur[1].append(d[a])
        else:
            cur = [a, [d[a]]]
            out.append(cur)
    return " ".join("%#x:%s" % (a, bytes(b).hex()) for a, b in out[:4]) + (" ..." if len(out) > 4 else "")


def compare_states(emu, hw, flags=("cf", "pf", "af", "zf", "sf", "of", "df")):
    """Parts of the state that differ between two completed runs: [(part, emulator text, hardware text)]."""
    out = []
    if emu["rip"] != hw["rip"]:
        out.append(("rip", "%#x" % emu["rip"], "%#x" % hw["rip"]))
    for i in range(16):
        if emu["regs"][i] != hw["regs"][i]:
            out.append(("reg:" + REG_NAMES[i], "%#x" % emu["regs"][i], "%#x" % hw["regs"][i]))
    for f in flags:
        a, b = emu["eflags"] >> FLAG_BITS[f] & 1, hw["eflags"] >> FLAG_BITS[f] & 1
        if a != b:
            out.append(("flag:" + f, str(a), str(b)))
    for i in range(16):
        a, b = emu["xmm"][16 * i:16 * i + 16], hw["xmm"][16 * i:16 * i + 16]
        if a != b:
            out.append(("xmm%d" % i, a.hex(), b.hex()))
    if "mxcsr" in emu and emu["mxcsr"] != hw["mxcsr"]:
        out.append(("mxcsr", "%#x" % emu["mxcsr"], "%#x" % hw["mxcsr"]))
    if emu["mem"] != hw["mem"]:
        keys = {a for a in set(emu["mem"]) | set(hw["mem"]) if emu["mem"].get(a) != hw["mem"].get(a)}
        out.append(("memory", runs({a: emu["mem"][a] for a in keys if a in emu["mem"]}) or "unchanged",
                    runs({a: hw["mem"][a] for a in keys if a in hw["mem"]}) or "unchanged"))
    return out


def hw_view(r):
    return {"rip": r.rip, "regs": r.gpr, "eflags": r.rflags, "xmm": r.xmm, "mxcsr": r.mxcsr,
            "mem": {a + i: d[i] for a, d in r.mem for i in range(len(d))}}


# ---- instructions sampled from a module ---------------------------------------------------------------

# Hand-written encodings (--module extra) for the translated mnemonics no other source covers.
EXTRA_VECTORS = [
    ("66a7", "cmpsw"), ("f366a7", "repe cmpsw"), ("f266a7", "repne cmpsw"),
    ("48af", "scasq"), ("f248af", "repne scasq"), ("f348af", "repe scasq"),
    ("0f1809", "prefetcht0 [rcx]"), ("0f1811", "prefetcht1 [rcx]"), ("0f1819", "prefetcht2 [rcx]"),
    ("0f0d01", "prefetch [rcx]"), ("0f0d09", "prefetchw [rcx]"),
    ("c1f005", "sal eax, 5"), ("d1f0", "sal eax, 1"), ("48d3f0", "sal rax, cl"), ("c0f103", "sal cl, 3"),
    ("66d1f2", "sal dx, 1"), ("49c1f621", "sal r14, 0x21"),
]

def harness_regions(D, session, mems):
    """{memory id: regions} for a difftest.Session: its relocated image (mapped read-write-execute,
    as the harness's Unicorn maps it), and the arena and stack of each memory image, at the
    harness's addresses.  Pass the result's values to hw_case."""
    image_blob = C.Blob(session.image_bytes)
    regions = {}
    for m in mems:
        mem = D.make_mem(*session.mem_kinds[m])
        regions[m] = (C.Region(D.IMAGE_BASE, image_blob, C.PROT_R | C.PROT_W | C.PROT_X),
                      C.Region(D.ARENA_BASE, C.Blob(mem[:D.ARENA_SIZE]), C.PROT_R | C.PROT_W),
                      C.Region(D.STACK_BASE, C.Blob(mem[D.ARENA_SIZE:]), C.PROT_R | C.PROT_W))
    return regions


def hw_case(D, c, ins, regions, peek):
    """The hardware case for one instruction-test case `c` of difftest (a difftest.Case whose
    func_rva is the instruction `ins`).  `peek(addr, n)` reads the initial memory of c's memory
    image (Session.peeker).  A control transfer gets its stop addresses: the next instruction
    and the target, worked out from the operand and the initial state, not from any oracle."""
    from lifter.translate import GPR
    stops = ()
    if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_RET)
            or ins.insn_name() in ("loop", "loope", "loopne", "jrcxz", "jecxz")):
        peek = D._overlay(peek, list(c.pokes))
        stops = [D.IMAGE_BASE + ins.address + ins.size]
        if ins.group(capstone.CS_GRP_RET):
            t = peek(c.regs[4], 8)
        else:
            op = ins.operands[0]
            if op.type == X.X86_OP_IMM:
                t = D.IMAGE_BASE + op.imm
            elif op.type == X.X86_OP_REG:
                t = c.regs[GPR[op.reg][1]] if op.reg in GPR else None
            else:
                t = peek(D.ea_of(ins, op, c.regs, c.gs_base), 8)
        if t is not None:
            stops.append(t & D.M64)
    return C.Case(regions, D.IMAGE_BASE + c.func_rva, gpr=c.regs, rflags=c.eflags, xmm=c.xmm, mxcsr=0x1F80,
                  gs_base=c.gs_base, pokes=c.pokes, stops=stops, tag=c.id)


def insn_crosscheck(args):
    import difftest as D
    from lifter import lift as L
    from lifter.discover import Program
    from lifter.image import Image, find_module

    if args.module == "vectors":
        # the project's hand-written instruction vectors (tests/vectors.py), in the self-test's image
        sys.path.insert(0, os.path.join(REPO, "tests"))
        import run_selftests as RS
        from vectors import INSN_VECTORS
        image = RS.insn_image("vectors")
        mod, shapes = image.name, {}
        work = os.path.join(OUT, mod)
        rvas = [rva for rva, _, _ in INSN_VECTORS]
    elif args.module == "extra":
        # translated mnemonics that neither the modules' samples nor tests/vectors.py contain
        chunks = {0x1000 + 16 * i: bytes.fromhex(h) for i, (h, _) in enumerate(EXTRA_VECTORS)}
        chunks[0x8000] = bytes((i * 167 + 13) & 0xFF for i in range(0x200))
        image = Image.synthetic("extra", 0x10000, chunks, [(0x1000, 0x2000)], [], image_base=0x180000000)
        mod, shapes = image.name, {}
        work = os.path.join(OUT, mod)
        rvas = sorted(r for r in chunks if r < 0x8000)
    else:
        image = Image.from_pe(args.dll or find_module(args.module), args.module)
        mod = image.name
        work = os.path.join(OUT, mod)
        lifted = os.path.join(work, "lifted")
        os.makedirs(lifted, exist_ok=True)
        report = os.path.join(lifted, mod + ".report.json")
        fresh = False
        if os.path.exists(report):
            with open(report) as f:
                fresh = json.load(f).get("stamp") == L.lifter_stamp(image)
        if not fresh:
            log("lifting %s (for the lifter's list of operand shapes)" % mod)
            L.lift_module(image, lifted, quiet=True)
        with open(os.path.join(lifted, mod + ".shapes.json")) as f:
            shapes = json.load(f)
        rng = random.Random(0x1257)                      # the instruction test's own sampling
        rvas = []
        for sh in sorted(shapes):
            pool = shapes[sh]["rvas"]
            rvas += rng.sample(pool, min(args.per_shape, len(pool)))
    prog = Program(image)
    recs = L.lift_instructions(image, rvas, os.path.join(work, "lifted_insn"), prog)
    s = D.Session(image, os.path.join(work, "lifted_insn"), os.path.join(work, "run"), jobs=args.jobs)
    mems = [s.add_mem("rand", k) for k in range(4)]
    peeks = [s.peeker(m) for m in mems]

    cases, insns = [], {}
    for rva in sorted(recs):
        if recs[rva]["status"] != "lifted":
            continue
        ins = insns[rva] = prog.decode(rva)
        for k in range(args.states):                     # exactly instruction_test's loop
            r1 = random.Random("insn:%s:%x:%d" % (mod, rva, k))
            r2 = random.Random("insn-mem:%s:%x:%d" % (mod, rva, k))
            regs, eflags, xmm, pokes = D.insn_state(ins, r1, peeks[k % len(mems)], k, r2)
            cases.append(D.Case(id=len(cases), key=rva, func_rva=rva, ranges=[[rva, rva + ins.size]],
                                end=rva + ins.size, insn_rva=rva, mem_id=mems[k % len(mems)], regs=regs,
                                eflags=eflags, xmm=xmm, seed=r1.getrandbits(64), pokes=pokes, label="%d" % k))
    if args.limit:
        cases = cases[:args.limit]
    log("%s: %d shapes, %d instructions x %d states = %d cases" % (mod, len(shapes), len(insns), args.states, len(cases)))

    t0 = time.time()
    emu = s.run_oracle(cases)
    t_emu = time.time() - t0
    log("  emulator: %.1f s" % t_emu)

    # the same cases for the hardware: same image, arena and stack at the same addresses
    regions = harness_regions(D, s, mems)
    control = set()
    hw_cases = []
    for c in cases:
        hc = hw_case(D, c, insns[c.key], regions[c.mem_id], peeks[mems.index(c.mem_id)])
        if hc.stops:
            control.add(c.id)
        hw_cases.append(hc)

    ora = C.Oracle(family="xc-" + mod, offline=args.offline, verbose=True)
    t0 = time.time()
    done = [0]

    def progress(n):
        done[0] += n
        log("  hardware: %d of %d sent cases answered (%.0f s)" % (done[0], len(hw_cases) - ora.from_cache, time.time() - t0))
    hw = ora.run(hw_cases, progress=progress)
    t_hw = time.time() - t0
    log("  hardware: %d from the cache, %d sent, %.1f s" % (ora.from_cache, ora.sent, t_hw))

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    totals = Counter()
    classes = defaultdict(lambda: {"cases": 0, "instructions": set(), "labels": Counter(), "examples": []})
    per_mn = defaultdict(Counter)
    for c, hc, r in zip(cases, hw_cases, hw):
        ins = insns[c.key]
        rec = recs[c.key]
        sh = rec["shape"]
        mn = " ".join(sh.split(" ")[:2]) if sh.startswith(("lock ", "rep ", "repne ")) else sh.split(" ")[0]
        o = emu[c.id]
        per_mn[mn]["cases"] += 1
        diffs, label = [], "open"
        e_kind = o["status"]
        if r.outcome == C.NO_ANSWER:
            totals["no hardware answer"] += 1
            per_mn[mn]["no answer"] += 1
            continue
        if r.outcome == C.REFUSED:
            totals["refused by the runner (%s)" % C.REASONS.get(r.detail, r.detail)] += 1
            per_mn[mn]["refused"] += 1
            continue
        if r.outcome not in (C.DONE, C.FAULT):
            totals["hardware run failed: " + r.describe()] += 1
            diffs.append(("outcome", e_kind, r.describe()))
        elif e_kind == "inconclusive":
            ek, ea = emu_fault_kind(o["reason"], o.get("address"))
            if r.outcome == C.DONE:
                diffs.append(("outcome", "emulator: " + ek, "hardware: completes"))
            else:
                hk, ha = hw_fault_kind(r)
                if ek != hk:
                    diffs.append(("fault kind", ek + ("" if ea is None else " at %#x" % ea), hk + ("" if ha is None else " at %#x" % ha)))
                elif ea is not None and ha is not None and ea != ha:
                    diffs.append(("fault address", "%s at %#x" % (ek, ea), "%s at %#x" % (hk, ha)))
                else:
                    totals["both fault the same way"] += 1
        else:
            ev = {"regs": list(o["regs"]), "eflags": o["eflags"], "xmm": bytes(o["xmm"]), "mem": {}}
            for addr, now in o["pages"].items():
                ev["mem"].update(changed_bytes(now, C.initial_mem(hc, addr, D.PAGE), addr))
            und, und_regs, drop = D.undefined_after(ins, c.regs, o["eflags"])
            if e_kind == "trap":
                what = o["trap_what"]
                ev["rip"] = D.IMAGE_BASE + o["trap_rva"] + (ins.size if what == "int3" else 0)
                want = {"#DE": "#DE", "int3": "int3", "ud2": "#UD"}.get(what, "#GP" if what.startswith("#GP") else what)
                if r.outcome == C.DONE:
                    diffs.append(("outcome", "emulator: " + what, "hardware: completes"))
                else:
                    hk, _ = hw_fault_kind(r)
                    if hk != want:
                        diffs.append(("fault kind", what, hk))
                    else:
                        diffs = compare_states(ev, hw_view(r))
                        if not diffs:
                            totals["both trap the same way, same state"] += 1
            elif r.outcome == C.FAULT:
                hk, ha = hw_fault_kind(r)
                diffs.append(("outcome", "emulator: completes", "hardware: " + hk + ("" if ha is None else " at %#x" % ha)))
            else:
                calls = o["calls"]
                if calls and calls[0][0] in (D.K_CPUID, D.K_RDTSC):
                    totals["cpuid/rdtsc (stubbed by the harness)"] += 1
                    continue
                if calls:                                # undo the stub effect: the state at the transfer
                    k = calls[0]
                    ev["rip"] = k[1]
                    for idx, v in ((4, k[2]), (1, k[3]), (2, k[4]), (8, k[5]), (9, k[6]), (0, k[7]), (10, k[8]), (11, k[9])):
                        ev["regs"][idx] = v
                    ev["xmm"] = bytes(k[11]) + ev["xmm"][96:]
                elif ins.group(capstone.CS_GRP_RET):
                    ev["rip"] = D.SENTINEL
                else:
                    ev["rip"] = D.IMAGE_BASE + c.end
                diffs = compare_states(ev, hw_view(r))
                if diffs:
                    # mechanical labels
                    parts = {d[0] for d in diffs}
                    undef_parts = {"flag:" + f for f in und} | {"reg:" + REG_NAMES[i] for i in und_regs}
                    if drop or parts <= undef_parts:
                        label = "undefined"
                    else:
                        fixed = dict(ev, regs=D.oracle_corrections(ins, c, {"regs": ev["regs"], "eflags": ev["eflags"]}))
                        left = {d[0] for d in compare_states(fixed, hw_view(r))} - undef_parts
                        if fixed["regs"] != ev["regs"] and not left:
                            label = "known:cmpxchg32"
                elif r.unexpected_rip and c.id not in control:
                    diffs.append(("runner", "", "rip %#x is not the next instruction by the runner's own decoding" % r.rip))
        if not diffs:
            totals["agree"] += 1
            per_mn[mn]["agree"] += 1
            continue
        totals["disagree"] += 1
        per_mn[mn]["disagree: " + label] += 1
        und_now = sorted(D.undefined_after(ins, c.regs, o.get("eflags", 0))[0]) if e_kind != "inconclusive" else []
        for part, a, b in diffs:
            cl = classes["%s | %s | %s" % (mn, part, label)]
            cl["cases"] += 1
            cl["instructions"].add(c.key)
            if len(cl["examples"]) < 4:
                cl["examples"].append({"rva": "%#x" % c.key, "text": rec["text"], "state": c.label, "emulator": a,
                                       "hardware": b, "undefined_flags_for_this_input": und_now,
                                       "all_parts": sorted(d[0] for d in diffs)})
    out = {"module": mod, "cases": len(cases), "instructions": len(insns), "states": args.states,
           "per_shape": args.per_shape, "cpu": ora.header["cpu"] if ora.header else (hw[0].cpu if hw else None),
           "emulator_seconds": round(t_emu, 1), "hardware_seconds": round(t_hw, 1), "sent": ora.sent,
           "from_cache": ora.from_cache, "totals": dict(totals.most_common()),
           "per_mnemonic": {m: dict(v) for m, v in sorted(per_mn.items())},
           "classes": {k: {"cases": v["cases"], "instructions": len(v["instructions"]), "examples": v["examples"]}
                       for k, v in sorted(classes.items())}}
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "insn_%s.json" % mod), "w") as f:
        json.dump(out, f, indent=1)
    print_insn(out)
    return 0


def print_insn(out):
    print("%s: %d instructions x %d states = %d cases on %s" % (out["module"], out["instructions"], out["states"],
                                                              out["cases"], out["cpu"]))
    for k, v in out["totals"].items():
        print("  %7d  %s" % (v, k))
    print("  disagreements by mnemonic | part | label (cases, instructions, first example: emulator / hardware)")
    for k, v in out["classes"].items():
        e = v["examples"][0]
        print("  %-52s %6d %4d   %s [%s]: %s / %s" % (k, v["cases"], v["instructions"], e["text"], e["state"],
                                                     e["emulator"][:40], e["hardware"][:60]))


# ---- the SSE float operations of experiments/afp ---------------------------------------------------------

FLOAT_CODE = 0x0000200000000000


def _only_denormals_flushed(emu, hw, width, lanes):
    """True if hw equals emu except that result lanes holding a denormal in emu hold a zero of
    the same sign in hw (and at least one lane is like that)."""
    n, flushed = width // 8, 0
    if emu[n * lanes:] != hw[n * lanes:]:
        return False
    for j in range(lanes):
        x = int.from_bytes(emu[n * j:n * j + n], "little")
        y = int.from_bytes(hw[n * j:n * j + n], "little")
        if x == y:
            continue
        mant = (1 << (23 if width == 32 else 52)) - 1
        expo = (x >> (23 if width == 32 else 52)) & (0xFF if width == 32 else 0x7FF)
        if expo == 0 and x & mant and y == x & (1 << (width - 1)):
            flushed += 1
        else:
            return False
    return flushed > 0


def float_crosscheck(args):
    """Every operation of experiments/afp (afp_lab.OPS), under every MXCSR setting it is defined
    for, over that experiment's own inputs: its Unicorn loop (afp_x86.X86, as it is) against one
    hardware case per input record.  Three answers are compared per record: the emulator's, the
    experiment's SDM-based model (the emulator's answer with afp_lab.nan_patch and ftz_patch
    applied - what docs/findings/11-float-mode.md calls the corrected oracle), and the CPU's."""
    import afp_lab
    from afp_lab import OPS, configs_for, mxcsr_for, records_for, Settings, ftz_patch, nan_patch, classify
    from afp_x86 import X86, encode

    Settings.n_uniform, Settings.n_struct = args.uniform, args.structured
    ops = [o for o in OPS if afp_lab.match(o, args.ops.split(",") if args.ops else None)]
    page = bytearray(C.PAGE)
    slot = {}
    for i, op in enumerate(sorted(OPS)):
        enc, kind, _ = encode(op)
        page[16 * i:16 * i + len(enc)] = enc
        slot[op] = (FLOAT_CODE + 16 * i, kind)
    regions = (C.Region(FLOAT_CODE, bytes(page), C.PROT_R | C.PROT_X),)

    def pick(segs, default):
        """Record indices for one (op, config): all of the uniform and structured records the
        settings produced, and an evenly spaced sample of the boundary cross product."""
        idx = []
        for name, lo, hi in segs:
            n = hi - lo
            want = n if name != "boundary" else min(n, args.boundary if default else args.boundary_other)
            if name != "boundary" and not default:
                want = min(n, max(1, n // 3))
            idx += [lo + k * n // want for k in range(want)] if want else []
        return idx

    x86 = X86()
    plan = []                                            # (op, cfg, record indices, records, emulator out, model out, which patch)
    t0 = time.time()
    for op in ops:
        spec = OPS[op]
        recs, segs, _info = records_for(op)
        for cfg in configs_for(op):
            rc, daz, ftz = cfg
            idx = pick(segs, cfg == (0, 0, 0))
            sub = b"".join(recs[32 * i:32 * i + 32] for i in idx)
            raw = x86.run(op, sub, mxcsr_for(rc, daz, ftz))
            model, after_nan = raw, raw
            if op[:3] in ("add", "sub", "mul", "div") and op[3:] in ("ss", "sd", "ps", "pd"):
                model, _ = nan_patch(spec, sub, model)
                after_nan = model
            if ftz and spec["ftzm"]:
                model, _ = ftz_patch(spec, sub, model, rc, daz)
            plan.append((op, cfg, sub, raw, after_nan, model))
    n_cases = sum(len(p[2]) // 32 for p in plan)
    t_emu = time.time() - t0
    log("float: %d operations, %d (operation, MXCSR) settings, %d cases; emulator and model %.1f s" % (
        len(ops), len(plan), n_cases, t_emu))

    cases = []
    for op, cfg, sub, _raw, _an, _model in plan:
        addr, kind = slot[op]
        mx = mxcsr_for(*cfg)
        for i in range(len(sub) // 32):
            a, b = sub[32 * i:32 * i + 16], sub[32 * i + 16:32 * i + 32]
            gpr = [0] * 16
            gpr[0] = int.from_bytes(a[:8], "little")
            cases.append(C.Case(regions, addr, gpr=gpr, mxcsr=mx, xmm=(b if kind == "g2x" else a) + b + bytes(224)))
    ora = C.Oracle(family="xc-float", offline=args.offline, verbose=True, batch_seconds=6.0)
    t0 = time.time()
    done = [0]

    def progress(n):
        done[0] += n
        log("  hardware: %d of %d sent cases answered (%.0f s)" % (done[0], len(cases) - ora.from_cache, time.time() - t0))
    hw = ora.run(cases, progress=progress)
    t_hw = time.time() - t0
    log("  hardware: %d from the cache, %d sent, %.1f s" % (ora.from_cache, ora.sent, t_hw))

    totals = Counter()
    classes = defaultdict(lambda: {"cases": 0, "by_input": Counter(), "examples": []})
    per_op = defaultdict(Counter)
    pos = 0
    for op, cfg, sub, raw, after_nan, model in plan:
        spec = OPS[op]
        kind = slot[op][1]
        rc, daz, ftz = cfg
        for i in range(len(sub) // 32):
            c, r = cases[pos], hw[pos]
            pos += 1
            per_op[op]["cases"] += 1
            if r.outcome == C.NO_ANSWER:
                totals["no hardware answer"] += 1
                continue
            e, m = raw[24 * i:24 * i + 24], model[24 * i:24 * i + 24]
            label = None
            if r.outcome != C.DONE or r.unexpected_rip:
                h, label = b"", "open: hardware did not complete (%s)" % r.describe()
            else:
                rax = (r.rflags & 0x8D5) if kind == "flags" else r.gpr[0]
                h = r.xmm[:16] + struct.pack("<Q", rax)
                rest_same = r.xmm[16:] == c.xmm[16:] and r.gpr[1:] == [x & C.M64 for x in c.gpr[1:]] and not r.mem
                if not rest_same:
                    label = "open: state outside xmm0/rax changed"
                elif h == e == m:
                    totals["agree (emulator = model = hardware)"] += 1
                    per_op[op]["agree"] += 1
                    continue
                elif h == m:
                    label = "known: FTZ ignored by the emulator" if after_nan[24 * i:24 * i + 24] == e \
                        else "known: NaN operand choice" if after_nan[24 * i:24 * i + 24] == m \
                        else "known: FTZ and NaN choice"
                    label += " (model right)"
                elif op.startswith(("rcp", "rsqrt")):
                    label = "known: rcp/rsqrt is an approximation on hardware"
                elif op[:3] in ("min", "max") and daz and _only_denormals_flushed(e, h, afp_lab.DOM_W[spec["dom"]], spec["nl"]):
                    label = "known: min/max with DAZ return a selected denormal as zero"
                elif h == e:
                    label = "open: the afp model is wrong, the emulator is right"
                else:
                    label = "open"
            totals[label.split(":")[0] if label.startswith("known") else "open"] += 1
            per_op[op][label] += 1
            cl = classes["%s | rc=%d daz=%d ftz=%d | %s" % (op, rc, daz, ftz, label)]
            cl["cases"] += 1
            cat = classify(spec, sub, i)
            cl["by_input"][cat] += 1
            if len(cl["examples"]) < 3 or (cl["by_input"][cat] == 1 and len(cl["examples"]) < 8):
                cl["examples"].append({"a": sub[32 * i:32 * i + 16].hex(), "b": sub[32 * i + 16:32 * i + 32].hex(),
                                       "input": cat, "emulator": e.hex(), "model": m.hex(), "hardware": h.hex(),
                                       "hw_mxcsr": "%#x" % r.mxcsr})
    out = {"cases": n_cases, "operations": len(ops), "settings": len(plan), "uniform": args.uniform,
           "structured": args.structured, "boundary": args.boundary, "boundary_other": args.boundary_other,
           "cpu": ora.header["cpu"] if ora.header else (hw[0].cpu if hw else None),
           "emulator_seconds": round(t_emu, 1), "hardware_seconds": round(t_hw, 1), "sent": ora.sent,
           "from_cache": ora.from_cache, "totals": dict(totals.most_common()),
           "per_operation": {o: dict(v) for o, v in sorted(per_op.items())},
           "classes": {k: {"cases": v["cases"], "by_input": dict(v["by_input"]), "examples": v["examples"]}
                       for k, v in sorted(classes.items())}}
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "float%s.json" % ("" if not args.ops else "_partial")), "w") as f:
        json.dump(out, f, indent=1)
    print_float(out)
    return 0


def print_float(out):
    print("float operations: %d operations, %d settings, %d cases on %s" % (out["operations"], out["settings"],
                                                                            out["cases"], out["cpu"]))
    for k, v in out["totals"].items():
        print("  %7d  %s" % (v, k))
    merged = defaultdict(lambda: [0, Counter(), None, set()])
    for k, v in out["classes"].items():
        op, cfg, label = [x.strip() for x in k.split("|")]
        m = merged[(label, op)]
        m[0] += v["cases"]
        m[1].update(v["by_input"])
        m[2] = m[2] or (cfg, v["examples"][0])
        m[3].add(cfg)
    print("  disagreements by label, operation (cases; MXCSR settings; kinds of input; first example)")
    for (label, op), (n, cats, (cfg, e), cfgs) in sorted(merged.items()):
        print("  %-48s %-12s %6d  %2d settings  %s" % (label[:48], op, n, len(cfgs),
                                                     " ".join("%s %d" % kv for kv in cats.most_common(4))))
        print("      e.g. %s a=%s b=%s  emulator %s  model %s  hardware %s" % (
            cfg, e["a"][:16], e["b"][:16], e["emulator"][:16], e["model"][:16], e["hardware"][:16]))


# ---- one hand-written case per class of disagreement -------------------------------------------------------

def unicorn_one(case):
    """Run one hand-written single-instruction case in a fresh Unicorn (the emulator the harness
    uses), with nothing of the harness around it.  Returns (how it ended, state dict)."""
    import unicorn
    from unicorn import x86_const as U
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    for r in case.regions:
        uc.mem_map(r.addr, r.size, r.prot)
        if r.blob:
            uc.mem_write(r.addr, r.blob.data)
    for a, d in case.pokes:
        uc.mem_write(a, d)
    gpr = [U.UC_X86_REG_RAX, U.UC_X86_REG_RCX, U.UC_X86_REG_RDX, U.UC_X86_REG_RBX, U.UC_X86_REG_RSP, U.UC_X86_REG_RBP,
           U.UC_X86_REG_RSI, U.UC_X86_REG_RDI] + [getattr(U, "UC_X86_REG_R%d" % i) for i in range(8, 16)]
    xmm = [getattr(U, "UC_X86_REG_XMM%d" % i) for i in range(16)]
    for i in range(16):
        uc.reg_write(gpr[i], case.gpr[i] & C.M64)
        uc.reg_write(xmm[i], int.from_bytes(case.xmm[16 * i:16 * i + 16], "little"))
    uc.reg_write(U.UC_X86_REG_EFLAGS, 2 | (case.rflags & C.RFLAGS_VISIBLE))
    uc.reg_write(U.UC_X86_REG_MXCSR, case.mxcsr)
    uc.reg_write(U.UC_X86_REG_GS_BASE, case.gs_base)
    bad = []
    uc.hook_add(unicorn.UC_HOOK_MEM_READ_UNMAPPED | unicorn.UC_HOOK_MEM_WRITE_UNMAPPED | unicorn.UC_HOOK_MEM_FETCH_UNMAPPED,
                lambda u, access, addr, size, value, _: bad.append((access, addr)) and False)
    how = "completes"
    try:
        uc.emu_start(case.rip, 0, count=1)
    except unicorn.UcError as e:
        kind, _ = emu_fault_kind(str(e), None)
        how = kind + (" at %#x" % bad[0][1] if bad else "")
    st = {"rip": uc.reg_read(U.UC_X86_REG_RIP), "regs": [uc.reg_read(r) for r in gpr],
          "eflags": uc.reg_read(U.UC_X86_REG_EFLAGS) & C.RFLAGS_VISIBLE,
          "xmm": b"".join(uc.reg_read(r).to_bytes(16, "little") for r in xmm), "mem": {}}
    for r in case.regions:
        if r.prot & C.PROT_W:
            st["mem"].update(changed_bytes(bytes(uc.mem_read(r.addr, r.size)), C.initial_mem(case, r.addr, r.size), r.addr))
    return how, st


def named_cases(args):
    """Each class of disagreement the cross-checks found, as one case written by hand (no game
    bytes), run in Unicorn and on the hardware.  The table is what docs/handoff/hw-oracle.md
    quotes; `make hworacle-crosscheck` regenerates it, from the cache when the machine is off."""
    code_at, data, stack = 0x0000200000000000, 0x0000210000000000, 0x0000220000000000
    data_bytes = bytes((i * 11 + 5) & 0xFF for i in range(0x1000))

    def case(code_hex, **kw):
        regs = (C.Region(code_at, bytes.fromhex(code_hex), C.PROT_R | C.PROT_X),
                C.Region(data, data_bytes, C.PROT_R | C.PROT_W),
                C.Region(stack, None, C.PROT_R | C.PROT_W, size=0x2000))
        g = dict(rsp=stack + 0x1000)
        g.update(kw.pop("gpr", {}))
        return C.Case(regs, code_at, gpr=C.gpr_list(**g), **kw)

    def f32(lo):
        return (lo | 0x5A5A5A5A_A5A5A5A5_3C3C3C3C << 32).to_bytes(16, "little")
    x = lambda a, b: C.xmm_bytes({0: f32(a), 1: f32(b)})                             # noqa: E731
    table = [
        # (kind, name, text, case)
        ("known", "cmpxchg, 32-bit, equal: only the destination is written", "cmpxchg ecx, edx",
         case("0fb1d1", gpr=dict(rax=0xAAAAAAAA00000005, rcx=0xBBBBBBBB00000005, rdx=9))),
        ("known", "lock neg: SF and PF come from the result", "lock neg dword ptr [rbx]",
         case("f0f71b", gpr=dict(rbx=data + 0x100), pokes=[(data + 0x100, struct.pack("<I", 1))])),
        ("known", "MXCSR.FTZ flushes a tiny result", "mulss xmm0, xmm1 (2^-126 * 0.5, FTZ=1)",
         case("f30f59c1", xmm=x(0x00800000, 0x3F000000), mxcsr=0x9F80)),
        ("known", "two NaN operands: the first, quieted", "addss xmm0, xmm1 (sNaN 7f800001 + qNaN 7fe27b08)",
         case("f30f58c1", xmm=x(0x7F800001, 0x7FE27B08))),
        ("known", "rcpss is a table approximation", "rcpss xmm0, xmm1 (1.0)", case("f30f53c1", xmm=x(0, 0x3F800000))),
        ("known", "min/max under DAZ return a selected denormal as zero", "minss xmm0, xmm1 (1e-45, +inf, DAZ=1)",
         case("f30f5dc1", xmm=x(0x00000001, 0x7F800000), mxcsr=0x1FC0)),
        ("new", "aligned SSE load from a misaligned address is #GP", "movaps xmm0, xmmword ptr [rbx]  (rbx = 8 mod 16)",
         case("0f2803", gpr=dict(rbx=data + 0x108))),
        ("new", "aligned SSE store to a misaligned address is #GP", "movdqa xmmword ptr [rsp + 0x28], xmm6  (rsp = 0 mod 16)",
         case("660f7f742428", xmm=C.xmm_bytes({6: 0x1122334455667788})) ),
        ("new", "SSE operation with a misaligned memory operand is #GP", "andnps xmm0, xmmword ptr [rbx]  (rbx = 8 mod 16)",
         case("0f5503", gpr=dict(rbx=data + 0x108))),
        ("new", "indirect jump to a non-canonical address is #GP at the jump", "jmp rax  (rax = 0x0000800000000000)",
         case("ffe0", gpr=dict(rax=0x0000800000000000), stops=[0x0000800000000000])),
        ("new", "indirect call to a non-canonical address is #GP at the call (rsp kept; the return address is already written)",
         "call rax  (rax = 0x8000000000000000)",
         case("ffd0", gpr=dict(rax=0x8000000000000000), stops=[0x8000000000000000])),
        ("new", "read-modify-write of an unmapped address is a write fault", "inc byte ptr [rbx]  (rbx unmapped)",
         case("fe03", gpr=dict(rbx=data + 0x40000000))),
        ("undefined", "bsf with a zero source: destination undefined (SDM)", "bsf eax, ecx  (ecx = 0)",
         case("0fbcc1", gpr=dict(rax=0x1111111122222222, rcx=0))),
        ("undefined", "imul: ZF undefined (SDM)", "imul eax, ecx  (0 * 5, ZF clear going in)",
         case("0fafc1", gpr=dict(rax=0, rcx=5), rflags=0)),
        ("undefined", "shift by more than 1: OF undefined (SDM)", "shl eax, 5  (eax = 0x04000000)",
         case("c1e005", gpr=dict(rax=0x04000000), rflags=0)),
    ]
    ora = C.Oracle(family="xc-cases", offline=args.offline)
    hw = ora.run([t[3] for t in table])
    rows = []
    for (kind, name, text, c), r in zip(table, hw):
        how, st = unicorn_one(c)
        if r.outcome == C.NO_ANSWER:
            rows.append({"kind": kind, "name": name, "instruction": text, "emulator": how, "hardware": "no hardware answer"})
            continue
        if r.outcome == C.FAULT:
            hk, ha = hw_fault_kind(r)
            hw_how = hk + ("" if ha is None else " at %#x" % ha)
        else:
            hw_how = "completes" if r.outcome == C.DONE else r.describe()
        diffs = [("ends", how, hw_how)] if how != hw_how else []
        if how == hw_how == "completes":
            diffs += compare_states(st, hw_view(r))
        rows.append({"kind": kind, "name": name, "instruction": text, "bytes": C.initial_mem(c, c.rip, 15).rstrip(b"\0").hex(),
                     "emulator": how, "hardware": hw_how,
                     "differences": [{"part": d[0], "emulator": d[1], "hardware": d[2]} for d in diffs]})
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "cases.json"), "w") as f:
        json.dump(rows, f, indent=1)
    print("one hand-written case per class (emulator: Unicorn alone; hardware: %s)" % (hw[0].cpu or "from the cache"))
    for row in rows:
        print("  [%s] %s" % (row["kind"], row["name"]))
        print("        %s" % row["instruction"])
        for d in row.get("differences", []):
            print("        %-10s emulator %s | hardware %s" % (d["part"], d["emulator"], d["hardware"]))
        if not row.get("differences"):
            print("        (no difference: %s / %s)" % (row["emulator"], row["hardware"]))
    missing = [r["name"] for r in rows if r["kind"] != "undefined" and not r.get("differences")]
    return 1 if missing else 0


def report(args):
    merged = defaultdict(lambda: [0, 0, None, []])
    grand = Counter()
    mods = []
    for name in sorted(os.listdir(OUT)) if os.path.isdir(OUT) else []:
        if name.startswith("insn_") and name.endswith(".json"):
            with open(os.path.join(OUT, name)) as f:
                o = json.load(f)
            mods.append("%s %d" % (o["module"], o["cases"]))
            grand["cases"] += o["cases"]
            grand["instructions"] += o["instructions"]
            for k, v in o["totals"].items():
                grand[k] += v
            for k, v in o["classes"].items():
                m = merged[k]
                m[0] += v["cases"]
                m[1] += v["instructions"]
                m[2] = m[2] or v["examples"][0]
                m[3].append(o["module"])
            if args.verbose:
                print_insn(o)
                print()
    if mods:
        print("instruction cross-check, all modules: %s" % ", ".join(mods))
        print("  %d cases over %d instructions" % (grand.pop("cases"), grand.pop("instructions")))
        for k, v in grand.most_common():
            print("  %7d  %s" % (v, k))
        print("  ('agree' includes the cases where both sides trap or fault the same way)")
        by_label = Counter()
        for k, m in merged.items():
            by_label[k.split(" | ")[2]] += m[0]
        print("  differing parts by label: %s" % ", ".join("%s %d" % kv for kv in by_label.most_common()))
        print("  mnemonic | part | label                                  cases insns  modules; first example (emulator / hardware)")
        for k, (n, ni, e, where) in sorted(merged.items(), key=lambda kv: (kv[0].split(" | ")[2], kv[0])):
            print("  %-52s %6d %5d  %d; %s: %s / %s" % (k, n, ni, len(where), e["text"], e["emulator"][:44], e["hardware"][:60]))
        print()
    if os.path.exists(os.path.join(OUT, "float.json")):
        with open(os.path.join(OUT, "float.json")) as f:
            print_float(json.load(f))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["insn", "float", "cases", "report"])
    ap.add_argument("--module", help="insn: a module under game/; 'vectors' for tests/vectors.py's hand-written "
                    "ones; 'extra' for the hand-written ones in this file")
    ap.add_argument("--dll")
    ap.add_argument("--per-shape", type=int, default=6, help="instructions sampled per operand shape")
    ap.add_argument("--states", type=int, default=24, help="states per instruction")
    ap.add_argument("--limit", type=int, default=0, help="only the first N cases (for trying things out)")
    ap.add_argument("--jobs", type=int)
    ap.add_argument("--ops", help="float: comma-separated patterns of operation names (default: all 90)")
    ap.add_argument("--uniform", type=int, default=300, help="float: uniform-random input pairs per domain")
    ap.add_argument("--structured", type=int, default=1500, help="float: structured-random input pairs per domain")
    ap.add_argument("--boundary", type=int, default=3000, help="float: boundary-product records under the default MXCSR")
    ap.add_argument("--boundary-other", type=int, default=700, help="float: the same under each other MXCSR setting")
    ap.add_argument("--offline", action="store_true", help="never open ssh; cases without a cached answer are counted")
    ap.add_argument("--verbose", action="store_true", help="report: also the table of each module")
    args = ap.parse_args()
    try:
        if args.mode == "insn":
            if not args.module:
                ap.error("insn needs --module")
            return insn_crosscheck(args)
        if args.mode == "float":
            return float_crosscheck(args)
        if args.mode == "cases":
            return named_cases(args)
        return report(args)
    except C.OracleUnavailable as e:
        print("x86 machine unavailable: %s" % e, file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
