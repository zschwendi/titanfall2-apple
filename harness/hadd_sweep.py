#!/usr/bin/env python3
"""addsubps and haddps: what the real CPU does, lane by lane, and the lifted translation against it.

    .venv/bin/python harness/hadd_sweep.py              cached answers, the x86 machine for the rest
    .venv/bin/python harness/hadd_sweep.py --offline    from the cache only (the normal way to re-run it)

The question (swarm brief lifter-insn-leftovers, item 2): finding 11 (docs/findings/11-float-mode.md)
had no measured formulation for these two SSE3 instructions, and the emulator is known wrong for
them under FTZ and where both inputs of a lane are NaNs.  So, measured on the CPU first:

  lanes      which input lanes each result lane is computed from, and whether they are added or
             subtracted (exact small integers, all eight input lanes different);
  two NaNs   where both inputs of a result lane are NaNs, whose payload the result carries.
             Every ordered pair of eight NaNs (four quiet, four signalling, both signs), so each
             pair is there in both orders;
  one NaN    the result is that NaN, quieted;
  invalid    infinity minus infinity gives the default NaN, ffc00000;
and then, against the translation (family sse_hadd, lifted, compiled and run by harness/driver.c
exactly as the instruction test runs it, in the guest float mode):
  values     the pairs above and infinities, signed zeros, denormals, rounding cases and the
             instruction test's own float pairs (floatcase.lane_pair), every bit of the state.

Everything is run under the seven MXCSR settings of the float tests (floatcase.VARIANTS).  Both
instructions are tested as `<op> xmm1, xmm2`; the expected side is a Core i5-7600K through
tools/hworacle (cache family hadd-sweep), asked once, in batches of at most BATCH cases.  The
emulator is not consulted.

A pair (a, b) is placed as the manual writes the lane: addsubps lane j is d[j] -/+ s[j], so a is
the destination's lane and b the source's; haddps lanes are d0 + d1, d2 + d3, s0 + s1, s2 + s3,
so a is the lower lane of the two and b the upper one.  "first" and "second" below mean that.
The pairs slide through the four result lanes: case i holds pairs i, i+1, i+2, i+3.

Without the family (or with one that refuses a mnemonic) the measurement still runs and the
comparison says which instruction has no translation.
"""

import argparse
import fcntl
import itertools
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

OUT = os.path.join(REPO, "harness", "out", "hadd_sweep")
FAMILY = "hadd-sweep"
BATCH = 4096                                         # cases per request to the x86 machine
VECTORS = {"addsubps": (0x1000, "f20fd0ca"), "haddps": (0x1010, "f20f7cca")}      # <op> xmm1, xmm2
OPS = tuple(VECTORS)
# The two inputs of each result lane as the Intel manual lists them: ('d' | 's', lane), first and second.
INPUTS = {"addsubps": [(("d", j), ("s", j)) for j in range(4)],
          "haddps": [(("d", 0), ("d", 1)), (("d", 2), ("d", 3)), (("s", 0), ("s", 1)), (("s", 2), ("s", 3))]}
SIGN, INF, QUIET = 0x80000000, 0x7F800000, 0x00400000
DEFAULT_NAN = 0xFFC00000
NANS = (0x7FC00001, 0xFFC12345, 0x7FFFFFFF, 0xFFC00000,            # quiet (the last is the default NaN itself)
        0x7F800002, 0xFF955555, 0x7FBFFFFF, 0xFF800001)            # signalling
ONE, MAXF, MINN = 0x3F800000, 0x7F7FFFFF, 0x00800000


def f32(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def pairs():
    """[(first, second, group)]: the lane pairs of the sweep, in a fixed order."""
    out = []
    for a, b in itertools.permutations(NANS, 2):                      # 56: every pair in both orders
        out.append((a, b, "two NaNs"))
    others = (ONE, SIGN | INF, 0, SIGN | 1, f32(-2.5), INF, SIGN, 0x007FFFFF)
    for i, n in enumerate(NANS):                                      # 32
        for x in (others[i], others[(i + 3) % 8]):
            out.append((n, x, "one NaN"))
            out.append((x, n, "one NaN"))
    for a, b in ((INF, INF), (INF, SIGN | INF), (SIGN | INF, INF), (SIGN | INF, SIGN | INF), (INF, ONE), (ONE, SIGN | INF),
                 (SIGN | INF, MAXF), (MAXF, INF), (INF, 1), (SIGN | 1, SIGN | INF), (INF, 0), (SIGN, SIGN | INF)):
        out.append((a, b, "infinities"))
    for a, b in ((0, 0), (0, SIGN), (SIGN, 0), (SIGN, SIGN), (f32(1.5), f32(-1.5)), (f32(1.5), f32(1.5)),
                 (f32(-1.5), f32(1.5)), (f32(-1.5), f32(-1.5)), (MAXF, SIGN | MAXF), (MINN, SIGN | MINN),
                 (0x00012345, 0x80012345), (0x00012345, 0x00012345)):
        out.append((a, b, "signed zeros and cancellation"))
    for a, b in ((1, 1), (1, SIGN | 2), (0x007FFFFF, 1), (MINN, SIGN | 1), (MINN, SIGN | 0x007FFFFF), (MINN | 1, SIGN | MINN),
                 (0x00FFFFFF, SIGN | MINN), (0x007FFFFF, ONE), (SIGN | 1, ONE), (1, 0), (0, SIGN | 1),
                 (SIGN | 0x007FFFFF, SIGN | 0x007FFFFF), (0x00400000, 0x00400000), (0x00C00000, SIGN | MINN),
                 (0x00C00001, MINN), (ONE, 0x00000001), (SIGN | 0x00000003, 0x00800002), (0x00800003, 0x80800001)):
        out.append((a, b, "denormals"))
    for a, b in ((ONE, f32(2.0 ** -24)), (ONE, f32(2.0 ** -24) + 1), (ONE, f32(-2.0 ** -25)), (ONE, f32(-2.0 ** -26)),
                 (f32(16777216.0), ONE), (f32(16777216.0), f32(3.0)), (f32(-16777216.0), f32(-1.0)), (MAXF, MAXF),
                 (SIGN | MAXF, SIGN | MAXF), (MAXF, f32(2.0 ** 103)), (MAXF, f32(2.0 ** 102)), (SIGN | MAXF, f32(-2.0 ** 103)),
                 (f32(0.1), f32(0.2)), (f32(1e30), ONE), (f32(-1e-30), ONE), (f32(0.3), f32(-0.1)), (MAXF, SIGN | f32(2.0 ** 103)),
                 (f32(1.0000001), f32(-1.0))):
        out.append((a, b, "rounding and overflow"))
    rng = random.Random("hadd-sweep:pairs")
    for i in range(40):
        a, b = FC.lane_pair(rng, 32, i % len(FC.CATEGORIES))
        out.append((a, b, "the instruction test's pairs"))
    return out


def is_nan(x):
    return FC.is_nan(x, 32)


class Plan:
    """The cases: difftest.Case objects, and for each what its lanes hold."""

    def __init__(self):
        self.cases, self.info = [], {}            # info: case id -> (op, variant, 'map' | 'pairs', d lanes, s lanes, pairs per lane)
        self.pairs = pairs()
        rng = random.Random("hadd-sweep:fill")
        n = len(self.pairs)
        for op in OPS:
            for vname, _ in FC.VARIANTS:
                for k in range(6):                # exact small integers: every input lane a different power of two
                    lanes = list(range(8))
                    random.Random("hadd-sweep:map:%d" % k).shuffle(lanes)
                    vals = [f32(float((1 << e) * (k + 1) * (-1 if (k >> 1) & 1 and e % 3 == 0 else 1))) for e in lanes]
                    self.add(op, vname, "map", vals[:4], vals[4:], None, rng)
                for i in range(n):
                    d, s, here = [None] * 4, [None] * 4, []
                    for j in range(4):
                        a, b, group = self.pairs[(i + j) % n]
                        for (reg, lane), v in zip(INPUTS[op][j], (a, b)):
                            (d if reg == "d" else s)[lane] = v
                        here.append((a, b, group))
                    self.add(op, vname, "pairs", d, s, here, rng)

    def add(self, op, vname, kind, d, s, here, rng):
        rva, code = VECTORS[op]
        xmm = bytearray(rng.getrandbits(8) for _ in range(256))       # every other register: random bits
        xmm[16:32] = b"".join(x.to_bytes(4, "little") for x in d)     # xmm1, the destination
        xmm[32:48] = b"".join(x.to_bytes(4, "little") for x in s)     # xmm2, the source
        regs = [rng.getrandbits(64) for _ in range(16)]
        regs[4] = D.RSP0
        size = len(code) // 2
        c = D.Case(id=len(self.cases), key=rva, func_rva=rva, ranges=[[rva, rva + size]], end=rva + size, insn_rva=rva,
                   mem_id=0, regs=regs, eflags=2, xmm=bytes(xmm), seed=1, mxcsr=dict(FC.VARIANTS)[vname], label=vname)
        self.cases.append(c)
        self.info[c.id] = (op, vname, kind, d, s, here)


def image():
    chunks = {rva: bytes.fromhex(h) for rva, h in VECTORS.values()}
    return Image.synthetic("hadd_sweep", 0x10000, chunks, [(0x1000, 0x2000)], [], image_base=0x180000000)


def session(img):
    """(session, {op: why it is not translated}): the two instructions lifted by the lifter as it
    is and built into the test driver.  The session is built even if neither is translated (the
    hardware cases are cut from its relocated image); the candidate is then not run."""
    lifted = os.path.join(OUT, "lifted_insn")
    recs = L.lift_instructions(img, sorted(rva for rva, _ in VECTORS.values()), lifted)
    missing = {op: "; ".join(x["what"] for x in recs[rva].get("reasons", [])) or recs[rva]["status"]
               for op, (rva, _) in VECTORS.items() if recs[rva]["status"] != "lifted"}
    s = D.Session(img, lifted, os.path.join(OUT, "run"))
    if len(missing) < len(OPS):
        s.build_driver()
    s.add_mem("rand", 0)
    return s, missing


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
    return C, res, note + ", %d asked now in batches of at most %d" % (asked, BATCH)


def lane_formulas(op, maps):
    """For each result lane: the formulas `x + y` / `x - y` over the eight input lanes that give the
    CPU's result in every map case.  maps: [(d lanes, s lanes, result lanes)], all exact."""
    val = lambda x: struct.unpack("<f", x.to_bytes(4, "little"))[0]
    names = ["d%d" % j for j in range(4)] + ["s%d" % j for j in range(4)]
    out = []
    for j in range(4):
        fits = []
        for x in range(8):
            for y in range(8):
                for sym in "+-":
                    if sym == "+" and y < x:
                        continue                  # x + y and y + x are one formula here: the values cannot tell them apart
                    ok = True
                    for d, s, r in maps:
                        ins = [val(v) for v in d + s]
                        want = ins[x] + ins[y] if sym == "+" else ins[x] - ins[y]
                        ok = ok and f32(want) == r[j]
                    if ok:
                        fits.append("%s %s %s" % (names[x], sym, names[y]))
        out.append(fits)
    return out


def run(offline=False, quiet=False):
    """The sweep.  Returns the summary (also written to harness/out/hadd_sweep/summary.json)."""
    say = (lambda *a: None) if quiet else print
    t0 = time.time()
    plan = Plan()
    s, untranslated = session(image())
    C, hw, note = ask_hardware(s, plan.cases, offline or os.environ.get("HWORACLE", "auto") == "offline")
    translated = [op for op in OPS if op not in untranslated]
    cand = s.run_candidate([c for c in plan.cases if plan.info[c.id][0] in translated]) if translated else {}

    tot = Counter()
    maps = defaultdict(list)                         # op -> [(d, s, result lanes)]
    nan2 = defaultdict(Counter)                      # (op, variant) -> first / second / other
    nan2_ex = defaultdict(list)
    nan1 = defaultdict(Counter)                      # (op, variant) -> ok / lanes
    inval = defaultdict(Counter)
    rows = defaultdict(Counter)                      # (op, group) -> lanes, agree, differs under <variant>
    examples = defaultdict(list)
    for c, r in zip(plan.cases, hw):
        op, vname, kind, d, sl, here = plan.info[c.id]
        tot["cases"] += 1
        if r.outcome != C.DONE or r.rip != D.IMAGE_BASE + c.end:
            tot["no hardware answer" if r.outcome == C.NO_ANSWER else "hardware did not complete"] += 1
            if r.outcome != C.NO_ANSWER and len(examples["hw"]) < 5:
                examples["hw"].append(r.describe())
            continue
        tot["judged"] += 1
        hx = bytes(r.xmm)
        res = [int.from_bytes(hx[16 + 4 * j:20 + 4 * j], "little") for j in range(4)]
        # the measurement: from the CPU's answers alone
        if kind == "map":
            maps[op].append((d, sl, res))
        else:
            for j, (a, b, group) in enumerate(here):
                if is_nan(a) and is_nan(b):
                    which = "first" if res[j] == FC.quieted(a, 32) else "second" if res[j] == FC.quieted(b, 32) else "other"
                    nan2[op, vname][which] += 1
                    if which != "first" and len(nan2_ex[op, which]) < 4:
                        nan2_ex[op, which].append("lane %d of %s(%08x, %08x) under %s: %08x" % (j, op, a, b, vname, res[j]))
                elif is_nan(a) or is_nan(b):
                    nan1[op, vname]["lanes"] += 1
                    nan1[op, vname]["ok"] += res[j] == FC.quieted(a if is_nan(a) else b, 32)
                elif is_nan(res[j]):
                    inval[op, vname]["lanes"] += 1
                    inval[op, vname]["ok"] += res[j] == DEFAULT_NAN
        # the comparison: the lifted translation, every bit of the state
        if op in untranslated:
            continue
        k = cand[c.id]
        tot["compared"] += 1
        kx = bytes(k["xmm"])
        rest = (k["status"] == 0 and hx[:16] == kx[:16] and hx[32:] == kx[32:] and list(r.gpr) == list(k["regs"])
                and not (r.mxcsr ^ k["mxcsr"]) & ~FC.MXCSR_STATUS)
        tot["rest of the state differs"] += not rest
        good = rest
        for j in range(4):
            got = int.from_bytes(kx[16 + 4 * j:20 + 4 * j], "little")
            group = "lanes (exact integers)" if kind == "map" else here[j][2]
            rows[op, group]["lanes"] += 1
            rows[op, group]["agree"] += got == res[j]
            if got != res[j]:
                good = False
                rows[op, group]["differs under " + vname] += 1
                if len(examples[op, group]) < 6:
                    what = "d=%s s=%s" % (" ".join("%08x" % x for x in d), " ".join("%08x" % x for x in sl)) if kind == "map" \
                        else "(%08x, %08x)" % here[j][:2]
                    examples[op, group].append("lane %d of %s %s under %s: CPU %08x, lifted %08x" % (j, op, what, vname, res[j], got))
        tot["agree"] += good

    say("addsubps/haddps sweep: %d cases, %d lane pairs in each of four lanes under %d MXCSR settings; %s" % (
        tot["cases"], len(plan.pairs), len(FC.VARIANTS), note))
    summary = {"cases": tot["cases"], "judged_by_the_cpu": tot["judged"], "no_hardware_answer": tot["no hardware answer"],
               "hardware_did_not_complete": tot["hardware did not complete"], "hardware": note, "measured": {},
               "untranslated": untranslated, "compared": tot["compared"], "cases_agreeing": tot["agree"],
               "rest_of_state_differs": tot["rest of the state differs"], "groups": [], "disagreeing_groups": []}
    measured_ok = tot["judged"] == tot["cases"] > 0
    for op in OPS:
        m = summary["measured"][op] = {"lanes": lane_formulas(op, maps[op]) if maps[op] else [], "map_cases": len(maps[op]),
                                       "two_nans": {}, "one_nan": {}, "invalid": {}}
        say("  %s, measured on the CPU:" % op)
        for j, fits in enumerate(m["lanes"]):
            say("    result lane %d = %s   (%d exact cases)" % (j, " or ".join(fits) or "NONE OF THE FORMULAS", len(maps[op])))
            measured_ok = measured_ok and len(fits) == 1
        for vname, _ in FC.VARIANTS:
            n2, n1, iv = nan2[op, vname], nan1[op, vname], inval[op, vname]
            m["two_nans"][vname] = dict(n2)
            m["one_nan"][vname] = [n1["ok"], n1["lanes"]]
            m["invalid"][vname] = [iv["ok"], iv["lanes"]]
            say("    %-10s two NaNs: the first's payload %d, the second's %d, something else %d; one NaN: that NaN quieted "
                "%d of %d; invalid: the default NaN %d of %d" % (vname, n2["first"], n2["second"], n2["other"], n1["ok"],
                                                                 n1["lanes"], iv["ok"], iv["lanes"]))
            measured_ok = measured_ok and n1["ok"] == n1["lanes"] and iv["ok"] == iv["lanes"] and not n2["other"] \
                and not (n2["first"] and n2["second"])
        for which in ("second", "other"):
            for e in nan2_ex[op, which]:
                say("      %s: %s" % (which, e))
    summary["measurement_consistent"] = measured_ok
    for op, why in untranslated.items():
        say("  %s is not translated (%s): nothing to compare" % (op, why))
    if rows:
        say("  the lifted translation against the CPU:")
        say("  %-9s %-30s %8s %8s" % ("op", "group", "lanes", "agree"))
    for key in sorted(rows):
        r = rows[key]
        summary["groups"].append({"op": key[0], "group": key[1], "lanes": r["lanes"], "agree": r["agree"]})
        say("  %-9s %-30s %8d %8d%s" % (key + (r["lanes"], r["agree"], "" if r["lanes"] == r["agree"] else "   DIFFERS")))
        if r["lanes"] != r["agree"]:
            dd = {"op": key[0], "group": key[1], "examples": examples[key],
                  "by_variant": {k[len("differs under "):]: v for k, v in r.items() if k.startswith("differs under ")}}
            summary["disagreeing_groups"].append(dd)
            say("      by MXCSR setting: %s" % ", ".join("%s %d" % kv for kv in sorted(dd["by_variant"].items())))
            for e in examples[key]:
                say("      " + e)
    for e in examples["hw"]:
        say("  hardware did not complete: " + e)
    say("  judged by the CPU: %d of %d cases; compared with the translation: %d, of which %d agree in every bit of the "
        "state and %d do not (state outside the result lanes differs in %d)" % (
            tot["judged"], tot["cases"], tot["compared"], tot["agree"], tot["compared"] - tot["agree"],
            tot["rest of the state differs"]))
    summary["seconds"] = round(time.time() - t0)
    summary["ok"] = bool(measured_ok and not untranslated and tot["compared"] == tot["cases"] == tot["agree"])
    verdict = ("AGREES" if summary["ok"] else "INCOMPLETE" if tot["judged"] != tot["cases"] else
               "DOES NOT AGREE" if tot["compared"] != tot["agree"] or not measured_ok else "MEASURED, NOT ALL TRANSLATED")
    summary["verdict"] = verdict
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1)
    say("HADD-SWEEP %s (%d s)" % (verdict, summary["seconds"]))
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--offline", action="store_true", help="never open ssh: cached answers only")
    a = ap.parse_args()
    return 0 if run(a.offline)["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
