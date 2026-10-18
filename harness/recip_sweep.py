#!/usr/bin/env python3
"""rcpss, rsqrtss, rcpps, rsqrtps: the lifted translation against the real CPU, operand by operand.

    .venv/bin/python harness/recip_sweep.py              cached answers, the x86 machine for the rest
    .venv/bin/python harness/recip_sweep.py --offline    from the cache only (the normal way to re-run it)
    .venv/bin/python harness/recip_sweep.py --quick      the special operands only (a few seconds)

The question (swarm brief lifter-recip, item 1): the translation of these four is one host frecpe /
frsqrte per lane in the guest float mode (lifter/insns/sse_recip.py, tf_frecpe32 / tf_frsqrte32 in
harness/runtime.h).  Is that the Intel function for every table entry and every special operand,
under every MXCSR setting the float test uses?

Both sides run the same single instruction on the same state:
  candidate   the lifted C of `<op> xmm1, xmm2`, compiled and run by harness/driver.c exactly as
              the instruction test runs it (guest float mode, FPCR from the case's MXCSR);
  expected    a Core i5-7600K through tools/hworacle (cache family recip-sweep), one
              register-only case per operand, asked in batches of at most BATCH cases.
The emulator is not consulted: it computes the exact quotient (see the control at the end).

What is swept, for rcp and for rsqrt:
  table       every value of the top 12 bits of the mantissa (4,096: each of the table's 2,048
              entries twice) at four exponents, with the low 11 bits zero, all ones, or mixed.
              rcp: biased exponents 127, 128, 1 and 252 (the last whose results are normal:
              from 253 on the result would be denormal and is a zero instead, which the special
              operands cover); rsqrt: 127, 128, 1, 254 (both parities twice).  Scalar instruction under the default MXCSR; packed instruction,
              the four exponents in its four lanes, under each of the six other settings.
  special     both zeros, both infinities, quiet and signalling NaNs with payloads, denormals,
              the smallest and the largest normals, the operands around the zero-result
              threshold of rcp, negative operands, and 512 random bit patterns: scalar and
              packed, under all seven settings.
A case agrees when every register, all of XMM (the result lanes, the lanes a scalar form must
keep, the other fifteen registers) and the control bits of MXCSR are equal.

This is the small, case-by-case companion of tools/hworacle/sweep.py, which compared all 2^32
operands under all 16 MXCSR settings by hash against the bare host instruction; this one goes
through the translator, the helper and the driver, and keeps every answer.
"""

import argparse
import fcntl
import json
import os
import random
import struct
import sys
import time
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
for p in (REPO, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

import difftest as D                                 # noqa: E402
import floatcase as FC                               # noqa: E402
from lifter import lift as L                         # noqa: E402
from lifter.image import Image                       # noqa: E402

OUT = os.path.join(REPO, "harness", "out", "recip_sweep")
FAMILY = "recip-sweep"
BATCH = 4096                                         # cases per request to the x86 machine
VECTORS = {("rcp", 1): (0x1000, "f30f53ca"), ("rsqrt", 1): (0x1010, "f30f52ca"),      # <op>ss xmm1, xmm2
           ("rcp", 4): (0x1020, "0f53ca"), ("rsqrt", 4): (0x1030, "0f52ca")}          # <op>ps xmm1, xmm2
TABLE_EXP = {"rcp": (127, 128, 1, 252), "rsqrt": (127, 128, 1, 254)}
SIGN, INF, QUIET = 0x80000000, 0x7F800000, 0x00400000


def table_operand(t, k, salt):
    """Top 12 mantissa bits t, in exponent slot k: the low 11 bits are zero, all ones, or mixed."""
    low = (0, 0x7FF, (t * 0x9E5 + 0x2F1 * salt + 0x155) & 0x7FF, (t * 0x3C7 + 0x1B3 * salt + 0x2AA) & 0x7FF)[(k + salt) % 4]
    return t << 11 | low


def special_operands():
    """[(bits, why)] without duplicates: the operands a table sweep does not reach."""
    out, seen = [], set()

    def add(x, why):
        if x not in seen:
            seen.add(x)
            out.append((x, why))
    for x in FC.boundary(32):
        add(x, "boundary")
    for s in (0, SIGN):
        for m in (1, 2, 0x000FFF, 0x100000, 0x1FFFFF, 0x200000, 0x3FFFFF, 0x400000, 0x400001, 0x555555, 0x600000,
                  0x7FFFFE, 0x7FFFFF):
            add(s | m, "denormal")
        for e in (1, 2, 3):                                           # the smallest normals
            for m in (0, 1, 0x000800, 0x400000, 0x7FF800, 0x7FFFFF):
                add(s | e << 23 | m, "small normal")
        for e in (251, 252, 253, 254):                                # the largest: rcp's result leaves the normal range
            for m in (0, 1, 0x000800, 0x400000, 0x7FC800, 0x7FD000, 0x7FD7FF, 0x7FD800, 0x7FDFFF, 0x7FE000, 0x7FE800,
                      0x7FF000, 0x7FF800, 0x7FFFFF):
                add(s | e << 23 | m, "large normal")
        for p in (1, 0x2AAAAA, 0x3FFFFF, 0x155555, 0x0BADF0):         # NaNs with payloads, quiet and signalling
            add(s | INF | QUIET | p, "nan")
            add(s | INF | p, "nan")
    rng = random.Random("recip-sweep:special")
    for _ in range(512):
        add(rng.getrandbits(32), "random")
    return out


def classify(op, x):
    """The class of one operand, for the report."""
    e, m, neg = (x >> 23) & 0xFF, x & 0x7FFFFF, bool(x & SIGN)
    sign = "-" if neg else "+"
    if e == 255:
        return sign + "infinity" if m == 0 else ("quiet NaN" if m & QUIET else "signalling NaN")
    if e == 0:
        return sign + ("zero" if m == 0 else "denormal")
    if op == "rsqrt" and neg:
        return "-normal"
    return sign + "normal"


class Plan:
    """The cases of the sweep: difftest.Case objects, and for each what its lanes hold."""

    def __init__(self, quick):
        self.cases, self.info = [], {}            # info: case id -> (op, lanes, variant, group, [operand per lane])
        rng = random.Random("recip-sweep:fill")
        specials = special_operands()
        for op in ("rcp", "rsqrt"):
            if not quick:
                for k, e in enumerate(TABLE_EXP[op]):
                    for t in range(4096):
                        self.add(op, 1, "default", "table", [e << 23 | table_operand(t, k, 0)], rng)
                for vname, _ in FC.VARIANTS[1:]:
                    for t in range(4096):
                        self.add(op, 4, vname, "table", [e << 23 | table_operand(t, k, 1 + (t & 1))
                                                         for k, e in enumerate(TABLE_EXP[op])], rng)
            for vname, _ in FC.VARIANTS:
                for x, _ in specials:
                    self.add(op, 1, vname, "special", [x], rng)
                for i in range(0, len(specials), 3):                  # packed: four neighbours, sliding by three
                    self.add(op, 4, vname, "special", [specials[(i + j) % len(specials)][0] for j in range(4)], rng)

    def add(self, op, lanes, vname, group, operands, rng):
        rva, code = VECTORS[op, lanes]
        xmm = bytearray(rng.getrandbits(8) for _ in range(256))       # every other lane and register: random bits
        xmm[32:32 + 4 * lanes] = b"".join(x.to_bytes(4, "little") for x in operands)
        regs = [rng.getrandbits(64) for _ in range(16)]
        regs[4] = D.RSP0
        size = len(code) // 2
        c = D.Case(id=len(self.cases), key=rva, func_rva=rva, ranges=[[rva, rva + size]], end=rva + size, insn_rva=rva,
                   mem_id=0, regs=regs, eflags=2, xmm=bytes(xmm), seed=1, mxcsr=dict(FC.VARIANTS)[vname], label=vname)
        self.cases.append(c)
        self.info[c.id] = (op, lanes, vname, group, operands)


def session():
    """The four instructions, lifted by the lifter as it is and built into the test driver."""
    chunks = {rva: bytes.fromhex(h) for rva, h in VECTORS.values()}
    image = Image.synthetic("recip_sweep", 0x10000, chunks, [(0x1000, 0x2000)], [], image_base=0x180000000)
    lifted = os.path.join(OUT, "lifted_insn")
    recs = L.lift_instructions(image, sorted(chunks), lifted)
    bad = {hex(r): v.get("reasons") for r, v in recs.items() if v["status"] != "lifted"}
    if bad:
        raise SystemExit("recip sweep: the lifter does not translate %s" % bad)
    s = D.Session(image, lifted, os.path.join(OUT, "run"))
    s.build_driver()
    s.add_mem("rand", 0)
    return s


def ask_hardware(s, cases, offline):
    """One hworacle Result per case (NO_ANSWER where there is none) and a note on what happened.

    Cached answers first.  The misses go to the machine BATCH cases at a time, one batch at a
    time (the harness's lock); if it does not answer, nothing is retried: the rest stay
    unanswered, and the harness's marker keeps every other test from asking for a while."""
    C = FC.client()
    page = C.Region(D.IMAGE_BASE + 0x1000, C.Blob(s.image_bytes[0x1000:0x2000]), C.PROT_R | C.PROT_X)
    hw = [C.Case((page,), D.IMAGE_BASE + c.func_rva, gpr=c.regs, rflags=c.eflags, xmm=c.xmm, mxcsr=c.mxcsr, tag=c.id)
          for c in cases]
    res = C.Oracle(family=FAMILY, offline=True).run(hw)
    missing = [i for i, r in enumerate(res) if r.outcome == C.NO_ANSWER]
    note = "%d answers from the cache" % (len(res) - len(missing))
    if not missing:
        return C, res, note
    if offline:
        return C, res, note + ", %d never asked (offline)" % len(missing)
    try:
        if time.time() - os.path.getmtime(FC._DOWN) < FC.DOWN_SECONDS:
            return C, res, note + ", %d not asked: the machine did not answer less than %d s ago" % (len(missing), FC.DOWN_SECONDS)
    except OSError:
        pass
    asked = 0
    for b in range(0, len(missing), BATCH):
        part = missing[b:b + BATCH]
        with open(FC._LOCK, "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                got = C.Oracle(family=FAMILY).run([hw[i] for i in part])
            except C.OracleUnavailable as e:
                why = str(e).strip().split("\n")[-1][:200]
                with open(FC._DOWN, "w") as f:
                    f.write(why + "\n")
                return C, res, note + ", %d asked now, then the machine did not answer (%s): %d left unanswered" % (
                    asked, why, len(missing) - asked)
        for i, r in zip(part, got):
            res[i] = r
        asked += len(part)
        print("  asked %d of %d" % (asked, len(missing)), file=sys.stderr, flush=True)
    return C, res, note + ", %d asked now in batches of at most %d" % (asked, BATCH)


def exact_quotient(op, x):
    """What `1.0f / x` or `1.0f / sqrtf(x)` gives for a positive normal operand (to the nearest float)."""
    v = struct.unpack("<f", x.to_bytes(4, "little"))[0]
    q = 1.0 / v if op == "rcp" else 1.0 / (v ** 0.5)
    try:
        return int.from_bytes(struct.pack("<f", q), "little")
    except OverflowError:
        return INF


def run(quick=False, offline=False, quiet=False):
    """The sweep.  Returns the summary (also written to harness/out/recip_sweep/summary.json)."""
    say = (lambda *a: None) if quiet else print
    t0 = time.time()
    plan = Plan(quick)
    s = session()
    C, hw, note = ask_hardware(s, plan.cases, offline or os.environ.get("HWORACLE", "auto") == "offline")
    cand = s.run_candidate(plan.cases)
    rows = defaultdict(Counter)                      # (op, group, class) -> lanes, agree, per-variant disagreements
    examples = defaultdict(list)
    tot = Counter()
    control = Counter()
    for c, r in zip(plan.cases, hw):
        op, lanes, vname, group, operands = plan.info[c.id]
        tot["cases"] += 1
        if r.outcome != C.DONE or r.rip != D.IMAGE_BASE + c.end:
            tot["no hardware answer" if r.outcome == C.NO_ANSWER else "hardware did not complete"] += 1
            if r.outcome != C.NO_ANSWER and len(examples["hw"]) < 5:
                examples["hw"].append(r.describe())
            continue
        k = cand[c.id]
        tot["judged"] += 1
        tot["lanes"] += lanes
        tot["hardware changed MXCSR"] += r.mxcsr != c.mxcsr
        hx, kx = bytes(r.xmm), bytes(k["xmm"])
        rest = (k["status"] == 0 and hx[:16] == kx[:16] and hx[16 + 4 * lanes:] == kx[16 + 4 * lanes:]
                and list(r.gpr) == list(k["regs"]) and not (r.mxcsr ^ k["mxcsr"]) & ~FC.MXCSR_STATUS)
        tot["rest of the state differs"] += not rest
        good = rest
        for j, x in enumerate(operands):
            want = int.from_bytes(hx[16 + 4 * j:20 + 4 * j], "little")
            got = int.from_bytes(kx[16 + 4 * j:20 + 4 * j], "little")
            key = (op, group, classify(op, x) if group == "special" else "exponent %d" % (x >> 23 & 0xFF))
            rows[key]["lanes"] += 1
            rows[key]["agree"] += want == got
            if want != got:
                good = False
                rows[key]["differs under " + vname] += 1
                if len(examples[key]) < 6:
                    examples[key].append("%s%s(%08x) under %s: CPU %08x, lifted %08x" % (op, "ss" if lanes == 1 else "ps", x, vname, want, got))
            if 0 < x >> 23 < 255:                                     # a positive normal operand: the control
                control[op, "lanes"] += 1
                control[op, "equal"] += exact_quotient(op, x) == want
        tot["agree"] += good
    summary = {"quick": quick, "cases": tot["cases"], "judged_by_the_cpu": tot["judged"], "lanes": tot["lanes"],
               "cases_agreeing": tot["agree"], "no_hardware_answer": tot["no hardware answer"],
               "hardware_did_not_complete": tot["hardware did not complete"],
               "rest_of_state_differs": tot["rest of the state differs"], "hardware_changed_mxcsr": tot["hardware changed MXCSR"],
               "hardware": note, "classes": [], "disagreeing_classes": [],
               "exact_quotient_equals_cpu": {op: [control[op, "equal"], control[op, "lanes"]] for op in ("rcp", "rsqrt")}}
    say("rcp/rsqrt sweep: %d cases (%s); %s" % (tot["cases"], "special operands only" if quick else "table and special operands", note))
    say("  %-6s %-8s %-16s %8s %8s" % ("op", "group", "class", "lanes", "agree"))
    for key in sorted(rows):
        r = rows[key]
        summary["classes"].append({"op": key[0], "group": key[1], "class": key[2], "lanes": r["lanes"], "agree": r["agree"]})
        say("  %-6s %-8s %-16s %8d %8d%s" % (key + (r["lanes"], r["agree"], "" if r["lanes"] == r["agree"] else "   DIFFERS")))
        if r["lanes"] != r["agree"]:
            d = {"op": key[0], "group": key[1], "class": key[2], "examples": examples[key],
                 "by_variant": {k[len("differs under "):]: v for k, v in r.items() if k.startswith("differs under ")}}
            summary["disagreeing_classes"].append(d)
            say("      by MXCSR setting: %s" % ", ".join("%s %d" % kv for kv in sorted(d["by_variant"].items())))
            for e in examples[key]:
                say("      " + e)
    for e in examples["hw"]:
        say("  hardware did not complete: " + e)
    say("  judged by the CPU: %d of %d cases, %d result lanes; %d cases agree in every bit of the state, %d do not" % (
        tot["judged"], tot["cases"], tot["lanes"], tot["agree"], tot["judged"] - tot["agree"]))
    say("  no hardware answer %d, hardware did not complete %d; state outside the result lanes differs in %d; "
        "the CPU changed MXCSR in %d" % (tot["no hardware answer"], tot["hardware did not complete"],
                                         tot["rest of the state differs"], tot["hardware changed MXCSR"]))
    say("  control: the exact quotient (what the emulator computes) equals the CPU's result in %d of %d rcp lanes "
        "and %d of %d rsqrt lanes with a normal operand" % (control["rcp", "equal"], control["rcp", "lanes"],
                                                            control["rsqrt", "equal"], control["rsqrt", "lanes"]))
    summary["seconds"] = round(time.time() - t0)
    summary["ok"] = tot["judged"] == tot["cases"] == tot["agree"] and tot["cases"] > 0
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1)
    say("RECIP-SWEEP %s (%d s)" % ("AGREES" if summary["ok"] else "DOES NOT AGREE" if tot["judged"] != tot["agree"] else "INCOMPLETE",
                                   summary["seconds"]))
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--offline", action="store_true", help="never open ssh: cached answers only")
    ap.add_argument("--quick", action="store_true", help="the special operands only")
    a = ap.parse_args()
    return 0 if run(a.quick, a.offline)["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
