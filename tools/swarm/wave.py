#!/usr/bin/env python3
"""Run a wave of units with headless Haiku workers, with no orchestrator turns in between.

For each unit: claim it, run a worker (tools/swarm/haiku_worker.sh, billed to the API key in
.env), run the ledger check, merge on a pass; on a failed check run one more worker with the
end of the check log, then give up and leave the unit claimed for a stronger model.  At the end
one suite is run once on the merged tree and the ledger is committed: the runtime suite
(`make rt-test`), or for units of kind insn-task the lifter's (`make test`, then
`make lift MODULE=tier0` and `make difftest-func MODULE=tier0`).

    .venv/bin/python tools/swarm/wave.py shim-sleep shim-tls ...        named units
    .venv/bin/python tools/swarm/wave.py --kind shim-task --max 40      todo units of a kind
    .venv/bin/python tools/swarm/wave.py --report swarm/logs/wave-....json   reprint a report

Options: --parallel N (default 6), --timeout-min M per worker run (default 30), --attempts K
(default 2), --no-final-test, --dry-run.

What the orchestrating session reads is the table this prints at the end; everything else is
in swarm/logs/ (git-ignored): <id>.worker.<n>.jsonl (the worker's event stream),
<id>.report.<n>.md (its final reply), wave-<time>.json (this run's numbers).

Costs are computed here from each request's token counts with the price list below (the CLI's
own figure is wrong for this model).  A request whose prompt is over 100,000 tokens is billed
at five times the rate: the table shows each worker's largest prompt so that oversize tasks
can be made smaller.
"""

import argparse
import concurrent.futures
import glob
import json
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = os.path.join(ROOT, ".venv", "bin", "python")
SWARM = os.path.join(ROOT, "tools", "swarm", "swarm.py")
WORKER = os.path.join(ROOT, "tools", "swarm", "haiku_worker.sh")
LOGS = os.path.join(ROOT, "swarm", "logs")

# Dollars per million tokens for claude-haiku-5-5 (the owner's price sheet, 2026-10-09):
# (prompt up to 100,000 tokens, prompt over 100,000 tokens).
PRICE = {"input": (0.10, 0.50), "output": (0.50, 2.50), "cache_write_5m": (0.125, 0.625),
         "cache_write_1h": (0.20, 1.00), "cache_read": (0.01, 0.05)}
TIER = 100_000

# The end-of-wave suite depends on what was merged: instruction families are judged by the
# lifter's tests, everything else (shims, runtime) by the runtime suite.
LIFTER_KINDS = ("insn-task",)
LIFTER_SUITE = (["make", "test"], ["make", "lift", "MODULE=tier0"], ["make", "difftest-func", "MODULE=tier0"])

git_lock = threading.Lock()          # claim and merge touch the main checkout and the worktree list


def run(cmd, timeout=None, cwd=ROOT):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        return 124, e.stdout or "", "timed out"


def unit(uid):
    with open(os.path.join(ROOT, "swarm", "units", uid + ".json")) as f:
        return json.load(f)


def usage_of(jsonl_path):
    """Token counts and cost of one worker run, from its event stream."""
    tot = dict(requests=0, input=0, output=0, cache_write=0, cache_read=0, max_prompt=0,
                            over_tier=0, usd=0.0, reply="", error="", api_error="")
    try:
        lines = open(jsonl_path, errors="replace").read().splitlines()
    except OSError:
        return tot
    per = {}                                 # message id -> its last usage
    final_out = 0                            # the run's total output tokens, from the closing event
    for line in lines:
        try:
            ev = json.loads(line)
        except ValueError:
            continue
        if ev.get("type") == "result":
            tot["reply"] = ev.get("result") or ""
            final_out = (ev.get("usage") or {}).get("output_tokens", 0)
            if ev.get("is_error"):
                tot["error"] = ev.get("subtype") or "error"
            if ev.get("terminal_reason") == "api_error" and not tot["api_error"]:
                tot["api_error"] = "api_error: %s" % (ev.get("result") or "")[:120]
        if ev.get("type") != "assistant":
            continue
        if ev.get("error"):                  # the API refused the request: billing, authentication, overload
            text = " ".join(str(c.get("text", "")) for c in (ev.get("message") or {}).get("content", [])
                            if isinstance(c, dict))
            tot["api_error"] = "%s: %s" % (ev.get("error"), text[:120])
        msg = ev.get("message") or {}
        u = msg.get("usage") or {}
        if u:
            old = per.get(msg.get("id"))
            if old is None or u.get("output_tokens", 0) >= old.get("output_tokens", 0):
                per[msg.get("id")] = u
    for u in per.values():
        cc = u.get("cache_creation") or {}
        w1h = cc.get("ephemeral_1h_input_tokens", 0)
        w5m = cc.get("ephemeral_5m_input_tokens", u.get("cache_creation_input_tokens", 0) - w1h)
        inp, out, rd = u.get("input_tokens", 0), u.get("output_tokens", 0), u.get("cache_read_input_tokens", 0)
        prompt = inp + rd + w5m + w1h
        t = 1 if prompt > TIER else 0
        tot["requests"] += 1
        tot["input"] += inp
        tot["output"] += out
        tot["cache_write"] += w5m + w1h
        tot["cache_read"] += rd
        tot["max_prompt"] = max(tot["max_prompt"], prompt)
        tot["over_tier"] += t
        tot["usd"] += (inp * PRICE["input"][t] + out * PRICE["output"][t] + w5m * PRICE["cache_write_5m"][t]
                       + w1h * PRICE["cache_write_1h"][t] + rd * PRICE["cache_read"][t]) / 1e6
    # The per-request events carry the output count from the start of each reply, which is
    # too low; the closing event has the run's true total.  Charge the difference at the
    # share of requests that were over the tier.
    if final_out > tot["output"] and tot["requests"]:
        share = tot["over_tier"] / tot["requests"]
        rate = PRICE["output"][0] * (1 - share) + PRICE["output"][1] * share
        tot["usd"] += (final_out - tot["output"]) * rate / 1e6
        tot["output"] = final_out
    return tot


# Set by the first worker whose run ended in an API error (the account's credit, the key, an
# outage).  Such a run says nothing about the unit: it is not an attempt, the unit is not
# checked, and no further worker is started, because each would fail the same way.  (Found
# the hard way on 2026-10-09: "Credit balance is too low" burned both attempts of 15 units.)
api_down = threading.Event()
# Which API errors mean "the service will refuse the next worker too".  Anything else that ends a
# run (max_output_tokens: the worker wrote a file in one reply) is that worker's failed attempt.
OUTAGE = re.compile(r"billing|credit balance|authentication|api key|permission|rate.?limit|overloaded|quota|"
                    r"service unavailable|internal server error|connection|timed? ?out", re.I)


def do_unit(uid, args):
    rec = dict(id=uid, outcome="", attempts=0, usd=0.0, requests=0, max_prompt=0, over_tier=0,
               minutes=0.0, note="")
    t0 = time.time()
    if api_down.is_set():
        rec.update(outcome="not-started", note="an earlier worker hit an API error; the wave was stopped")
        return rec
    with git_lock:
        rc, out, err = run([PY, SWARM, "claim", uid])
    if not os.path.isdir(os.path.join(ROOT, ".wt", uid)):
        rec.update(outcome="not-claimed", note=(out + err).strip().splitlines()[-1][:160] if (out + err).strip() else "")
        return rec
    extra = ""
    for attempt in range(1, args.attempts + 1):
        rec["attempts"] = attempt
        stream = os.path.join(LOGS, "%s.worker.%d.jsonl" % (uid, attempt))
        with open(stream, "w") as so, open(stream + ".err", "w") as se:
            try:
                wrc = subprocess.run([WORKER, uid, extra], stdout=so, stderr=se, cwd=ROOT,
                                     timeout=args.timeout_min * 60).returncode
            except subprocess.TimeoutExpired:
                wrc = 124
        u = usage_of(stream)
        with open(os.path.join(LOGS, "%s.report.%d.md" % (uid, attempt)), "w") as f:
            f.write(u["reply"] or "(no reply; worker exit status %d)\n" % wrc)
        rec["usd"] += u["usd"]
        rec["requests"] += u["requests"]
        rec["max_prompt"] = max(rec["max_prompt"], u["max_prompt"])
        rec["over_tier"] += u["over_tier"]
        if u["api_error"] and OUTAGE.search(u["api_error"]):
            api_down.set()
            rec["attempts"] = attempt - 1
            rec.update(outcome="api-error", note=u["api_error"][:160])
            break
        if u["api_error"]:
            # The worker's own doing (a reply over the output limit, a malformed request): the run
            # ended early, so check what it left and retry with the reason, like any failed attempt.
            worker_error = ("\nThe worker before you was stopped by this error: %s. If it is about the "
                            "length of a reply: never write a whole file in one reply; create it with its "
                            "first part and add the rest in several edits.\n" % u["api_error"][:200])
        else:
            worker_error = ""
        if u["requests"] == 0:
            rec.update(outcome="worker-did-not-run", note="exit %d; see %s.err" % (wrc, os.path.basename(stream)))
            break
        rc, out, err = run([PY, SWARM, "check", uid], timeout=3600)
        last = (out + err).strip().splitlines()[-1] if (out + err).strip() else ""
        if "PASSED" in last:
            with git_lock:
                rc, out, err = run([PY, SWARM, "merge", uid])
            ok = "merged" in (out + err)
            rec.update(outcome="merged" if ok else "merge-failed", note="" if ok else (out + err).strip()[-160:])
            break
        rec.update(outcome="check-failed", note=last[:160])
        if api_down.is_set():
            break
        logs = sorted(glob.glob(os.path.join(LOGS, uid + ".[0-9]*.log")), key=os.path.getmtime)
        tail = "".join(open(logs[-1], errors="replace").readlines()[-40:]) if logs else last
        extra = (worker_error + "\nThis is attempt %d. An earlier worker left commits in this worktree, and the unit's check then "
                 "failed. Start with `git log --oneline -8` and `git status --short`, keep what is right, and fix "
                 "what the check reports. The end of the check's log:\n\n%s" % (attempt + 1, tail[-6000:]))
        extra = extra.replace("\x00", "")              # a NUL from a log cannot be passed as an argument
    rec["minutes"] = round((time.time() - t0) / 60, 1)
    rec["usd"] = round(rec["usd"], 4)
    return rec


def final_suite(kinds):
    """Run the end-of-wave suite for the kinds of the merged units; return its one-line result."""
    parts = []
    if any(k not in LIFTER_KINDS for k in kinds):
        rc, out, err = run(["make", "rt-test"], timeout=3600)
        lines = [x for x in out.splitlines() if " tests, " in x]
        parts.append(lines[-1].strip() if lines else "make rt-test exit %d" % rc)
    if any(k in LIFTER_KINDS for k in kinds):
        for cmd in LIFTER_SUITE:
            rc, out, err = run(cmd, timeout=3600)
            lines = [x.strip() for x in out.splitlines() if x.strip()]
            said = [x for x in lines if x.startswith(("PASSED", "FAILED")) or (x.startswith("passing ") and ", failing " in x)]
            parts.append("%s: %s" % (" ".join(cmd), said[-1].split("   ")[0] if said else "exit %d" % rc if rc else "ok"))
            if rc:                               # the next command needs this one's output
                break
    return "; ".join(parts)


def table(recs, final):
    print("\n%-28s %-18s %3s %8s %5s %9s %5s %6s  %s" % ("unit", "outcome", "try", "usd", "reqs", "max prompt",
                                                          ">100k", "min", "note"))
    for r in recs:
        print("%-28s %-18s %3d %8.4f %5d %9d %5d %6.1f  %s" % (r["id"], r["outcome"], r["attempts"], r["usd"],
              r["requests"], r["max_prompt"], r["over_tier"], r["minutes"], r["note"][:70]))
    n = len(recs)
    merged = sum(r["outcome"] == "merged" for r in recs)
    print("\n%d units: %d merged, %d not; $%.2f in all; %d requests over 100,000 tokens; final suite: %s" % (
        n, merged, n - merged, sum(r["usd"] for r in recs), sum(r["over_tier"] for r in recs), final or "not run"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("units", nargs="*")
    ap.add_argument("--kind")
    ap.add_argument("--max", type=int, default=1000)
    ap.add_argument("--parallel", type=int, default=6)
    ap.add_argument("--timeout-min", type=int, default=30)
    ap.add_argument("--attempts", type=int, default=2)
    ap.add_argument("--no-final-test", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--report")
    args = ap.parse_args()
    if args.report:
        d = json.load(open(args.report))
        table(d["units"], d.get("final"))
        return 0
    ids = list(args.units)
    if args.kind:
        merged = set()
        cands = []
        for path in sorted(glob.glob(os.path.join(ROOT, "swarm", "units", "*.json"))):
            u = json.load(open(path))
            if u.get("state") == "merged":
                merged.add(u["id"])
            elif u.get("state") == "todo" and u.get("kind") == args.kind:
                cands.append(u)
        cands.sort(key=lambda u: (u.get("priority", 50), u["id"]))
        ids += [u["id"] for u in cands if all(n in merged for n in u.get("needs", []))][:args.max]
    ids = list(dict.fromkeys(ids))           # a unit named on the command line and also selected by --kind runs once
    if not ids:
        print("nothing to run")
        return 0
    for uid in ids:
        if unit(uid).get("model", "haiku") != "haiku":
            sys.exit("%s is not a Haiku unit: wave.py runs Haiku workers on the API key only" % uid)
    rc, out, err = run(["git", "status", "--porcelain"])
    dirty = [l for l in out.splitlines() if not l[3:].startswith("swarm/")]
    if dirty:
        sys.exit("the main checkout has uncommitted changes outside swarm/ (%s ...): commit them first, and do not "
                 "edit the main checkout while a wave runs, or the merges are refused" % dirty[0].strip())
    print("wave: %d unit(s), %d at a time: %s" % (len(ids), args.parallel, " ".join(ids)))
    if args.dry_run:
        return 0
    os.makedirs(LOGS, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(args.parallel) as ex:
        recs = list(ex.map(lambda uid: do_unit(uid, args), ids))
    final = ""
    if not args.no_final_test and any(r["outcome"] == "merged" for r in recs):
        final = final_suite({unit(r["id"]).get("kind") for r in recs if r["outcome"] == "merged"})
    import fcntl                              # the ledger's cross-process lock (swarm.py, repo_lock)
    with open(os.path.join(ROOT, ".git", "swarm.lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        run(["git", "add", "swarm"])
        run(["git", "commit", "-q", "-m", "Ledger: wave of %d unit(s), %d merged\n\nCo-Authored-By: Claude Opus 5.5 "
             "<noreply@anthropic.com>" % (len(recs), sum(r["outcome"] == "merged" for r in recs)), "--", "swarm"])
    path = os.path.join(LOGS, "wave-%s.json" % time.strftime("%Y%m%d-%H%M%S"))
    json.dump(dict(units=recs, final=final), open(path, "w"), indent=1)
    table(recs, final)
    print("report: " + os.path.relpath(path, ROOT))
    if api_down.is_set():
        print("STOPPED: the API refused a request (see the api-error rows). Units marked api-error or not-started "
              "were not attempted; run `swarm.py resume` when no agent is working, then start the wave again.")
    return 0 if all(r["outcome"] == "merged" for r in recs) else 1


if __name__ == "__main__":
    sys.exit(main())
