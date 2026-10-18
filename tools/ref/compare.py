#!/usr/bin/env python3
"""Compare the real DLLs' answers with ours, rank by rank; and the queue self-test's failure rate.

usage:
  compare.py --ref results/calls.txt --ours ours.txt     per rank: SAME, DIFFERENT, NOT COMPARABLE
  compare.py --queue results/queue.txt                    failure rate with a 95 % interval, beside ours
  compare.py --self-test                                  checks this script on made-up results

`results/calls.txt` and `results/queue.txt` come from tools/ref/run_ref.sh on the machine with
the game (refcalls.exe against the real tier0.dll and vstdlib.dll).  `ours.txt` is our driver's
output with the checked values switched on:

    GATE2_VALUES=1 build/gate2/gate2 call 1-28 30-90 > ours.txt

Both programs are compiled from the same call tables and print the same lines (gate2/values.h):
R lines carry a rank's verdict, V lines every value it checked (E) or only recorded (M).

What the verdicts mean:
  SAME            both pass and every value both recorded is equal; or the E values that differ
                  were each compared with an expectation taken from the machine (a thread id,
                  an address, the amount of memory), which the line then lists
  DIFFERENT       one passes and the other does not; or a recorded-only value (M) differs; or
                  the two made different checks.  Both values are shown
  NOT COMPARABLE  one side did not run the rank to a verdict (skipped, our driver stops); why

Exit status: 0 when no rank is DIFFERENT, 1 when one is, 2 for a usage error.
The queue mode only prints; `--ours-queue FAILED/RUNS` replaces our measured numbers.
"""
import argparse
import math
import re
import sys

# Our own measurement of the queue self-test (docs/handoff/gate2-threads-2.md, "The counts"):
# GATE2_SELFTESTS=queue GATE2_TIMEOUT=15, failures = host fault + no answer.
OURS_QUEUE = [
    ("our normal build", 38, 200),
    ("our four builds that differ only in memory ordering (one behaviour, by that handoff)", 38 + 33 + 36 + 32, 800),
    # docs/handoff/lifter-patch-queue.md, gate2/queue_runs.py: the same run, 400 of each build in turn, after the
    # push routine was patched in the lifted code (lifter/guest_patches.toml, queue-push-one-read).
    ("ours lifted without the guest patch (GUEST_PATCHES=off), 2026-10-10", 84, 400),
    ("ours with the guest patch queue-push-one-read, which is the normal build from then on", 0, 400),
]
OURS_BAD_LINK = "ours: 177 of 200 runs of the normal build print it, 0 of 800 runs of the sequentially consistent builds"


class Rank:
    def __init__(self, rank):
        self.rank = rank
        self.status = None      # PASS FAIL SKIP STOP CRASH TIMEOUT ERROR
        self.checks = None
        self.name = ""
        self.msg = ""
        self.values = {}        # what -> (times, first, last, digest, kind)
        self.exceptions = []


def parse(text):
    """The ranks of a calls.txt or of the driver's output, and the remarks beside them."""
    ranks, notes = {}, []

    def get(n):
        return ranks.setdefault(n, Rank(n))

    for line in text.splitlines():
        f = line.split("\t")
        if f[0] == "V" and len(f) >= 8:
            get(int(f[1])).values[f[7]] = (int(f[2]), f[3], f[4], f[5], f[6])
        elif f[0] == "R" and len(f) >= 6:
            r = get(int(f[1]))
            r.status, r.checks, r.name = f[2], int(f[3]), f[5]
            r.msg = f[6] if len(f) > 6 else ""
        elif f[0] == "X" and len(f) >= 5:
            get(int(f[1])).exceptions.append(f"{f[2]} at {f[3]} ({f[4]})")
        elif f[0] in ("L", "S") or line.startswith("#"):
            notes.append(line.replace("\t", " "))
        else:
            # the driver's line for people: only its STOP lines carry something the R lines lack
            m = re.match(r"^\s*(\d+)\s+(PASS|FAIL|SKIP|STOP)\s+(.*)$", line)
            if m and (m.group(2) == "STOP" or int(m.group(1)) not in ranks or ranks[int(m.group(1))].status is None):
                r = get(int(m.group(1)))
                r.status, r.msg = m.group(2), m.group(3)
    return ranks, notes


def strip_name(msg, name):
    return msg[len(name) + 2:] if name and msg.startswith(name + ": ") else msg


def compare_rank(ref, ours):
    """(verdict, text) for one rank; either argument may be None."""
    if ref is None or ref.status is None:
        return "NOT COMPARABLE", "the reference results have no line for this rank"
    name = ref.name
    if ref.status == "SKIP":
        return "NOT COMPARABLE", f"the reference program did not run it: {ref.msg} (ours: {ours.status if ours and ours.status else 'no line'})"
    if ours is None or ours.status is None:
        return "NOT COMPARABLE", f"our driver's output has no line for this rank (the real DLLs: {ref.status})"
    ours_msg = strip_name(ours.msg, name)
    if ours.status in ("STOP", "SKIP"):
        return "NOT COMPARABLE", f"our driver gives no verdict: {ours.status}: {ours_msg} (the real DLLs: {ref.status}{': ' + ref.msg if ref.msg else ''})"
    if ref.status in ("CRASH", "TIMEOUT", "ERROR"):
        if ref.status == "ERROR":
            return "NOT COMPARABLE", f"the reference program could not run it: {ref.msg}"
        return "DIFFERENT", f"ours: {ours.status}{': ' + ours_msg if ours_msg else ''} | the real DLLs: {ref.status}: {ref.msg}"

    both = [w for w in ours.values if w in ref.values]
    only_ours = [w for w in ours.values if w not in ref.values]
    only_ref = [w for w in ref.values if w not in ours.values]
    differ = [w for w in both if ours.values[w][0] != ref.values[w][0] or ours.values[w][3] != ref.values[w][3]]

    def show(w):
        o, r = ours.values[w], ref.values[w]
        times = "" if o[0] == r[0] == 1 else f" ({o[0]} and {r[0]} values; first shown)"
        return f"\"{w}\": ours {o[1]}, the real DLLs {r[1]}{times}"

    if ours.status != ref.status:
        failing, text = (ref, ref.msg) if ref.status == "FAIL" else (ours, ours_msg)
        side = "the real DLLs fail" if failing is ref else "ours fails"
        extra = "; ".join(show(w) for w in differ[:3])
        return "DIFFERENT", f"ours: {ours.status}, the real DLLs: {ref.status}; {side}: {text}" + (f" | values: {extra}" if extra else "")
    if ours.status == "FAIL":
        if ours_msg == ref.msg:
            return "SAME", f"both fail the same check with the same value: {ref.msg}"
        return "DIFFERENT", f"both fail, differently | ours: {ours_msg} | the real DLLs: {ref.msg}"

    # both pass
    measured = [w for w in differ if ours.values[w][4] == "M" or ref.values[w][4] == "M"]
    if measured:
        return "DIFFERENT", "both pass their checks, but a recorded value differs: " + "; ".join(show(w) for w in measured[:4])
    if only_ours or only_ref:
        parts = []
        if only_ours:
            parts.append("checked only by ours: " + ", ".join(f'"{w}"' for w in only_ours[:3]))
        if only_ref:
            parts.append("checked only on the real DLLs: " + ", ".join(f'"{w}"' for w in only_ref[:3]))
        return "DIFFERENT", "both pass, but they did not make the same checks: " + "; ".join(parts)
    notes = ""
    if ours_msg != ref.msg and (ours_msg or ref.msg):
        notes = f" | remarks differ: ours {ours_msg or '(none)'}; the real DLLs {ref.msg or '(none)'}"
    if not ours.values and not ref.values:
        if ours.checks is None:
            return "SAME", "both pass (our output has no value lines: run the driver with GATE2_VALUES=1 for more than the verdict)" + notes
        return "SAME", "both pass; the rank records no values (its checks are written out by hand)" + notes
    if differ:
        return "SAME", (f"both pass; {len(both) - len(differ)} of {len(both)} checked values equal, {len(differ)} compared with an expectation "
                        f"taken from the machine: " + "; ".join(show(w) for w in differ[:4]) + (" ..." if len(differ) > 4 else "") + notes)
    return "SAME", f"both pass; all {len(both)} checked values equal" + notes


def compare(ref_text, ours_text, out):
    ref, ref_notes = parse(ref_text)
    ours, _ = parse(ours_text)
    for n in ref_notes:
        out(f"reference: {n}")
    counts = {"SAME": 0, "DIFFERENT": 0, "NOT COMPARABLE": 0}
    for n in sorted(set(ref) | set(ours)):
        verdict, text = compare_rank(ref.get(n), ours.get(n))
        counts[verdict] += 1
        name = (ref.get(n) or ours.get(n)).name or (ours.get(n).name if ours.get(n) else "")
        out(f"{n:3d}  {verdict:<14}  {name}: {text}" if name else f"{n:3d}  {verdict:<14}  {text}")
        for side, r in (("the real DLLs", ref.get(n)), ("ours", ours.get(n))):
            if r and r.exceptions and verdict != "SAME":
                out(f"       exceptions seen ({side}): " + "; ".join(r.exceptions[:4]))
    out(f"compare: {counts['SAME']} same, {counts['DIFFERENT']} different, {counts['NOT COMPARABLE']} not comparable, of {sum(counts.values())} ranks")
    return counts


def wilson(k, n, z=1.959964):
    """The Wilson score interval for k failures in n runs (95 % by default)."""
    if n == 0:
        return 0.0, 1.0
    p = k / n
    denom = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / denom
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / denom
    return max(0.0, centre - half), min(1.0, centre + half)


def rate_line(label, k, n):
    lo, hi = wilson(k, n)
    line = f"  {label}: {k} of {n} runs failed = {100 * k / n:.1f} %, 95 % interval {100 * lo:.1f} to {100 * hi:.1f} %" if n else f"  {label}: no runs"
    if n and k == 0:
        line += f" (no failure seen: with 95 % confidence the rate is below {100 * (1 - 0.05 ** (1 / n)):.2f} %)"
    return line


def queue(text, out, ours=None):
    runs, summary = [], None
    for line in text.splitlines():
        f = line.split("\t")
        if f[0] == "Q" and len(f) >= 5:
            runs.append((f[2], int(f[3]), int(f[4]), f[5] if len(f) > 5 else ""))
        elif f[0] == "S":
            summary = line
        elif f[0] == "L" or line.startswith("#"):
            out("reference: " + line.replace("\t", " "))
    kinds = {}
    for status, _, _, _ in runs:
        kinds[status] = kinds.get(status, 0) + 1
    measured = [r for r in runs if r[0] != "ERROR"]
    failed = [r for r in measured if r[0] != "PASS"]
    n, k = len(measured), len(failed)
    out(f"queue self-test on the real DLLs: {len(runs)} runs: " + (", ".join(f"{v} {s}" for s, v in sorted(kinds.items())) or "none"))
    if not summary:
        out("  (the file has no summary line: the run was cut short; the counts are of the runs that are there)")
    if kinds.get("ERROR"):
        out(f"  {kinds['ERROR']} runs could not be made (ERROR) and are left out of the rate")
    out(rate_line("the real DLLs on real x86", k, n))
    for label, ok, on in (ours if ours is not None else OURS_QUEUE):
        out(rate_line(label, ok, on))
    if n:
        lo, hi = wilson(k, n)
        olo, ohi = wilson(*(ours if ours is not None else OURS_QUEUE)[0][1:])
        apart = hi < olo or lo > ohi
        out("  the two 95 % intervals " + ("do not overlap: the rates differ" if apart else "overlap: these runs do not show the rates to differ"))
    causes = {}
    for status, _, _, msg in failed:
        key = f"{status}: {re.sub(r'(address |thread )[0-9a-fx]+', lambda m: m.group(1) + '..', msg)[:140]}"     # one line per cause, not per address
        causes[key] = causes.get(key, 0) + 1
    for key, v in sorted(causes.items(), key=lambda kv: -kv[1])[:8]:
        out(f"  {v:4d} x {key}")
    passed_ms = sorted(r[1] for r in measured if r[0] == "PASS")
    if passed_ms:
        out(f"  a passing run took {passed_ms[len(passed_ms) // 2]} ms (median), {passed_ms[-1]} ms at most")
    bad = sum(1 for r in runs if r[2])
    out(f"  runs that print \"Bad node link detected\": {bad} of {len(runs)} ({OURS_BAD_LINK})")
    return k, n


SELF_REF = "\n".join([
    "# refcalls calls: made-up results for the self-test",
    "L\tOK\ttier0.dll at 0x180000000",
    "V\t1\t1\t0x4\t0x4\taaaaaaaaaaaaaaaa\tE\tPlat_GetMemPageSize()",
    "R\t1\tPASS\t1\taaaaaaaaaaaaaaaa\tPlat_GetMemPageSize\t",
    "V\t6\t1\t0x10000\t0x10000\t1111111111111111\tE\twrite-locked: first dword",
    "V\t6\t1\t0x2f4\t0x2f4\t2222222222222222\tE\tthe writer's thread id",
    "R\t6\tPASS\t2\t3333333333333333\tCThreadSpinRWLock\t",
    "V\t16\t3\t0x24\t0x24\t4444444444444444\tE\tthe byte after",
    "R\t16\tFAIL\t3\t4444444444444444\tPlat_gmtime\tPlat_gmtime(5), inside the range, returns 22",
    "R\t29\tSKIP\t0\t0000000000000000\tBuilt-in self tests\tnot run by this program",
    "X\t40\t0xc0000005\ttier0.dll+0x1234\tunhandled",
    "R\t40\tCRASH\t0\t0000000000000000\tSome rank\texception 0xc0000005 at tier0.dll+0x1234",
    "V\t60\t1\t0xbeef\t0xbeef\t5555555555555555\tM\tthe draws, as one digest",
    "R\t60\tPASS\t1\t5555555555555555\tRandomFloatExp\t(20 of 5000 draws differ)",
    "V\t61\t1\t0x1\t0x1\t6666666666666666\tM\tthe draws, as one digest",
    "R\t61\tPASS\t1\t6666666666666666\tGaussian\t",
    "R\t62\tPASS\t0\t0000000000000000\tCoroutines\t",
    "V\t70\t1\t0x1\t0x1\t7777777777777777\tE\tfirst check",
    "V\t70\t1\t0x2\t0x2\t8888888888888888\tE\ta check ours never reached",
    "R\t70\tPASS\t2\t9999999999999999\tTwo paths\t",
    "R\t71\tFAIL\t0\t0000000000000000\tBoth fail\tx: got 0x5 (5), expected 0x6 (6)",
    "S\tcalls\tPASS=6\tFAIL=2\tSKIP=1\tCRASH=1\tTIMEOUT=0\tERROR=0\tof=10",
])
SELF_OURS = "\n".join([
    "gate2 call: the load completes; each rank loads again in its own process",
    "V\t1\t1\t0x4\t0x4\taaaaaaaaaaaaaaaa\tE\tPlat_GetMemPageSize()",
    "R\t1\tPASS\t1\taaaaaaaaaaaaaaaa\tPlat_GetMemPageSize\t",
    " 1  PASS  Plat_GetMemPageSize",
    "V\t6\t1\t0x10000\t0x10000\t1111111111111111\tE\twrite-locked: first dword",
    "V\t6\t1\t0x1004\t0x1004\tbbbbbbbbbbbbbbbb\tE\tthe writer's thread id",
    "R\t6\tPASS\t2\tcccccccccccccccc\tCThreadSpinRWLock\t",
    " 6  PASS  CThreadSpinRWLock",
    "V\t16\t6\t0x24\t0x24\tdddddddddddddddd\tE\tthe byte after",
    "R\t16\tPASS\t6\tdddddddddddddddd\tPlat_gmtime\t",
    "16  PASS  Plat_gmtime",
    "R\t40\tPASS\t0\t0000000000000000\tSome rank\t",
    "40  PASS  Some rank",
    "V\t60\t1\t0xbeef\t0xbeef\t5555555555555555\tM\tthe draws, as one digest",
    "R\t60\tPASS\t1\t5555555555555555\tRandomFloatExp\t(25 of 5000 draws differ)",
    "60  PASS  RandomFloatExp: (25 of 5000 draws differ)",
    "V\t61\t1\t0x2\t0x2\teeeeeeeeeeeeeeee\tM\tthe draws, as one digest",
    "R\t61\tPASS\t1\teeeeeeeeeeeeeeee\tGaussian\t",
    "61  PASS  Gaussian",
    "62  STOP  Coroutines: FATAL call to vstdlib.dll+0x303c0, a function the lifter skipped",
    "V\t70\t1\t0x1\t0x1\t7777777777777777\tE\tfirst check",
    "R\t70\tPASS\t1\t7777777777777777\tTwo paths\t",
    "70  PASS  Two paths",
    "R\t71\tFAIL\t0\t0000000000000000\tBoth fail\tx: got 0x5 (5), expected 0x6 (6)",
    "71  FAIL  Both fail: x: got 0x5 (5), expected 0x6 (6)",
    "call: 6 PASS, 1 FAIL, 0 SKIP, 1 STOP of 8",
])
SELF_QUEUE = "\n".join(
    ["# refcalls queue: made-up runs for the self-test", "L\tOK\tloaded"]
    + [f"Q\t{i}\tPASS\t{40 + i % 7}\t0\t(messages received by the listener: 0, 30, 0, none with FAIL)" for i in range(1, 393)]
    + [f"Q\t{i}\tCRASH\t12\t0\texception 0xc0000005 at tier0.dll+0x1d0a0 (reading address 0x0) on thread 300" for i in range(393, 398)]
    + ["Q\t398\tTIMEOUT\t15000\t0\tno answer after 15 s (stopped by this program)",
       "Q\t399\tFAIL\t50\t1\tRunTSQueueTests reported 1 failure(s) of its own",
       "Q\t400\tERROR\t0\t0\tthe rank's process could not be started, last error 5",
       "S\tqueue\truns=400\tPASS=392\tFAIL=1\tCRASH=5\tTIMEOUT=1\tERROR=1\truns_with_bad_node_link=1"])


def self_test():
    lines = []
    counts = compare(SELF_REF, SELF_OURS, lines.append)
    by_rank = {int(l[:3]): l for l in lines if l[:3].strip().isdigit()}
    want = {1: "SAME", 6: "SAME", 16: "DIFFERENT", 29: "NOT COMPARABLE", 40: "DIFFERENT", 60: "SAME", 61: "DIFFERENT",
            62: "NOT COMPARABLE", 70: "DIFFERENT", 71: "SAME"}
    bad = []
    for rank, verdict in want.items():
        if rank not in by_rank or by_rank[rank][5:19].strip() != verdict:
            bad.append(f"rank {rank}: expected {verdict}, got: {by_rank.get(rank, '(no line)')}")
    for rank, needle in ((6, "ours 0x1004, the real DLLs 0x2f4"), (6, "1 of 2 checked values equal"), (16, "the real DLLs fail: Plat_gmtime(5)"),
                         (40, "exception 0xc0000005 at tier0.dll+0x1234"), (60, "remarks differ: ours (25 of 5000"), (61, "ours 0x2, the real DLLs 0x1"),
                         (62, "FATAL call to vstdlib.dll+0x303c0"), (70, "a check ours never reached"), (29, "not run by this program")):
        if needle not in by_rank.get(rank, ""):
            bad.append(f"rank {rank}: the line lacks \"{needle}\": {by_rank.get(rank, '(no line)')}")
    if counts != {"SAME": 4, "DIFFERENT": 4, "NOT COMPARABLE": 2}:
        bad.append(f"the totals are {counts}")
    lo, hi = wilson(38, 200)                    # by hand: centre 0.19584, half-width 0.05417
    if abs(lo - 0.1417) > 0.0006 or abs(hi - 0.2500) > 0.0006:
        bad.append(f"the Wilson interval of 38 in 200 is {lo:.4f} to {hi:.4f}, not 0.1417 to 0.2500")
    lo, hi = wilson(0, 400)                     # 0 to z^2 / (n + z^2)
    if lo != 0.0 or abs(hi - 3.8415 / 403.8415) > 1e-5:
        bad.append(f"the Wilson interval of 0 in 400 is {lo} to {hi}")
    qlines = []
    k, n = queue(SELF_QUEUE, qlines.append)
    if (k, n) != (7, 399):
        bad.append(f"the made-up queue file has 7 failures in 399 measured runs; counted {k} in {n}")
    for needle in ("5 CRASH, 1 ERROR, 1 FAIL, 392 PASS, 1 TIMEOUT", "7 of 399 runs failed = 1.8 %", "do not overlap", "5 x CRASH: exception 0xc0000005 at tier0.dll+0x1d0a0",
                   "\"Bad node link detected\": 1 of 400"):
        if not any(needle in l for l in qlines):
            bad.append(f"the queue report lacks \"{needle}\"")
    cut = "\n".join(SELF_QUEUE.splitlines()[:102])          # a file cut short: 100 runs, no S line
    qlines = []
    k, n = queue(cut, qlines.append)
    if (k, n) != (0, 100) or not any("cut short" in l for l in qlines) or not any("below 2.95 %" in l for l in qlines):
        bad.append(f"a file cut short after 100 passing runs is not reported as such: {qlines}")
    for b in bad:
        print("compare.py self-test: FAILED:", b)
    if not bad:
        print(f"compare.py self-test: ok ({len(want)} made-up ranks, 2 queue files, the interval against a hand computation)")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", help="results/calls.txt from run_ref.sh")
    ap.add_argument("--ours", help="output of: GATE2_VALUES=1 build/gate2/gate2 call 1-28 30-90")
    ap.add_argument("--queue", help="results/queue.txt from run_ref.sh")
    ap.add_argument("--ours-queue", metavar="FAILED/RUNS", help="our own count, instead of the one in gate2-threads-2.md")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not (a.ref and a.ours) and not a.queue:
        ap.print_usage()
        return 2
    status = 0
    if a.ref or a.ours:
        if not (a.ref and a.ours):
            print("compare.py: --ref and --ours go together")
            return 2
        with open(a.ref, errors="replace") as f:
            ref_text = f.read()
        with open(a.ours, errors="replace") as f:
            ours_text = f.read()
        if compare(ref_text, ours_text, print)["DIFFERENT"]:
            status = 1
    if a.queue:
        ours = None
        if a.ours_queue:
            m = re.fullmatch(r"(\d+)/(\d+)", a.ours_queue)
            if not m or int(m.group(2)) == 0 or int(m.group(1)) > int(m.group(2)):
                print("compare.py: --ours-queue takes FAILED/RUNS, for example 38/200")
                return 2
            ours = [("ours, as given", int(m.group(1)), int(m.group(2)))]
        with open(a.queue, errors="replace") as f:
            queue(f.read(), print, ours)
    return status


if __name__ == "__main__":
    sys.exit(main())
