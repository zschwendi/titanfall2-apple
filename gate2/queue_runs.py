#!/usr/bin/env python3
"""Count how often tier0's queue self-test fails in one or more builds of the Gate 2 driver.

    make gate2-build && make gate2-build GUEST_PATCHES=off
    .venv/bin/python gate2/queue_runs.py --runs 400 build/gate2/gate2 build/gate2-unpatched/gate2

One RUN is one fresh process of the driver,

    GATE2_SELFTESTS=queue GATE2_TIMEOUT=<--timeout, 15> <driver> call 29

which loads tier0.dll and vstdlib.dll and has rank 29 call tier0's RunTSQueueTests(10000, 1)
once, in a child process that the driver stops after the time limit.  It is the run of
docs/handoff/gate2-threads-2.md (38 failures of 200 in the normal build) and, on the other
side, of tools/ref/refcalls.c's queue mode (the same call of the real DLL on real x86, one
process per run: 4 failures of 400).  A run PASSES when rank 29 reports PASS: the routine
returned true and printed no failure of its own.  Every other outcome is a failure, counted
by kind: a host fault (the driver names the signal), no answer within the time limit, a
FAIL of the routine's own, anything else.

The builds are run in turn, run 1 of each, then run 2 of each, on --workers processes at
once (2), so that all of them see the same machine.  Prints one line per build with a 95 %
Wilson interval, and writes one line per run to --log if given.  Reads nothing of the game
itself: the driver does.
"""

import argparse
import concurrent.futures
import math
import os
import re
import subprocess
import sys
import time


def wilson(k, n, z=1.959964):
    if n == 0:
        return 0.0, 1.0
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return max(0.0, c - h), min(1.0, c + h)


def one_run(binary, timeout):
    """-> (kind, the rank's line).  The outer limit is only for a driver that itself hangs."""
    env = dict(os.environ, GATE2_SELFTESTS="queue", GATE2_TIMEOUT=str(timeout))
    try:
        p = subprocess.run([binary, "call", "29"], env=env, capture_output=True, text=True, errors="replace", timeout=timeout + 60)
        out, status = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        return "driver hung", "the driver itself did not end within %d s" % (timeout + 60)
    line = next((ln for ln in out.splitlines() if re.match(r"\s*29\s+(PASS|FAIL|STOP|SKIP)\b", ln)), "")
    word = line.split()[1] if line else ""
    if word == "PASS" and status == 0:
        return "pass", line
    if "no answer after" in line:
        return "no answer", line
    m = re.search(r"\bSIG[A-Z]+\b", line)
    if m:
        return "fault " + m.group(0), line
    if word == "FAIL":
        return "FAIL of its own", line
    return "other (status %d)" % status, line or out[-300:].replace("\n", " | ")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binaries", nargs="+", help="Gate 2 drivers, for example build/gate2/gate2")
    ap.add_argument("--runs", type=int, default=400, help="runs of each driver (400)")
    ap.add_argument("--timeout", type=int, default=15, help="GATE2_TIMEOUT: seconds before a run counts as no answer (15)")
    ap.add_argument("--workers", type=int, default=2, help="runs going at once (2)")
    ap.add_argument("--log", help="write one line per run here")
    a = ap.parse_args()
    for b in a.binaries:
        if not os.access(b, os.X_OK):
            sys.exit("queue_runs: no driver at %s (make gate2-build)" % b)
    jobs = [(i, b) for i in range(a.runs) for b in a.binaries]
    counts = {b: {} for b in a.binaries}
    first = {}
    t0 = time.time()
    log = open(a.log, "w") if a.log else None
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
        for (i, b), (kind, line) in zip(jobs, pool.map(lambda j: one_run(j[1], a.timeout), jobs)):
            counts[b][kind] = counts[b].get(kind, 0) + 1
            first.setdefault((b, kind), line)
            if log:
                log.write("%s\t%d\t%s\t%s\n" % (b, i + 1, kind, line.strip()[:400]))
                log.flush()
    print("queue self-test, %d runs of each driver; a run is one process of `GATE2_SELFTESTS=queue GATE2_TIMEOUT=%d <driver> call 29`; "
          "%d at once; %.0f s; load average now %.0f" % (a.runs, a.timeout, a.workers, time.time() - t0, os.getloadavg()[0]))
    for b in a.binaries:
        c = counts[b]
        n = sum(c.values())
        k = n - c.get("pass", 0)
        lo, hi = wilson(k, n)
        print("%-34s failed %d of %d (%.1f %%; 95 %% interval %.1f to %.1f %%): %s" % (
            b, k, n, 100.0 * k / n, 100 * lo, 100 * hi, ", ".join("%d %s" % (v, kk) for kk, v in sorted(c.items()))))
    for (b, kind), line in sorted(first.items()):
        if kind != "pass":
            print("  first '%s' of %s: %s" % (kind, b, line.strip()[:300]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
