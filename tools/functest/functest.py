#!/usr/bin/env python3
"""The function test (harness/difftest.py func; docs/DESIGN.md section 8) on any of the modules of
gate3/modules.def (the 20 of the boot path and ui11), however large: in chunks, each a restartable job whose result is one file.

    functest.py start [--jobs 4]      the whole plan, detached (nohup, nice 10); returns at once
    functest.py status                per module: functions passing, failing, inconclusive, ... not yet run
    functest.py stop [--drain]        stop the detached run (running chunks are lost, finished ones kept;
                                      --drain: start nothing new and end when the running ones have)
    functest.py failures [MODULE]     every failing function so far, with its first mismatch
    functest.py report [MODULE ...]   per-module totals and coverage from the chunk files (also written
                                      to <scratch>/<module>/report.json)
    functest.py run [--jobs 4]        what `start` runs, in the foreground
    functest.py prepare MODULE        the module's image, memory files and driver (once per lift)
    functest.py chunk MODULE N        one chunk, now, in this process
    functest.py one MODULE RVA ...    these functions again, now; their records as JSON (nothing is kept)
                 [--reg REG=V[,V...]] ...     with these registers set at entry, every seed once per combination
                                              of the values: `one mileswin64 0xa720 --reg edx=0,1,2,9` aims the
                                              second argument at three entries of a jump table and past its end
                                              (a 32-bit name keeps the seed's upper half); `--reg '[rcx]:32=3,4'`
                                              sets the memory a register points at instead ([REG+OFF]:BITS, where
                                              the seed's register is a pointer); the record's `aimed` says how the
                                              cases of each setting ended
    functest.py plan                  the order of the jobs and what is left of it
    functest.py started [M+0xRVA ...] the functions a `gate3 run` log shows running, and their verdicts so far

How it is cut.  A module's lifted functions are dealt into N chunks by a hash of the module
name and the function's RVA (`chunk_of`), N = ceil(lifted / CHUNK) fixed when the module is
first prepared.  So chunks 0..k-1 of a module are a random sample of k/N of its functions that
does not depend on the order of the functions or on which chunks ran before; the sample is
extended by running chunk k.  A chunk is one process: the oracle in it, then the driver once.

What is where (all under the scratch directory, default build/functest of this worktree,
$FUNCTEST_DIR overrides; it can be deleted whole, finished chunks are then run again):

    <module>/static/          image.bin, its variants, the memory files, the driver: what depends
                              only on the module and its lift (difftest.function_test_prepare)
    <module>/prepared.json    which lift (cache key, entry) and which harness that was built from,
                              the lifted functions and N
    <module>/chunks/cNNNN.json  the result of a chunk: difftest's summary and one record per function
    <module>/work/            case and result files of chunks that are running (removed at their end)
    log/                      one log per job;  run.log: the scheduler's;  run.pid: its process

The lifted C is read from the shared cache (tools/liftcache; docs/handoff/gate3-build.md):
nothing is lifted into the worktree.  The driver is compiled here, from that C, with the
harness's own flags (-DHARNESS_ISOLATED), which the cache's objects are not.

A chunk's result counts while the module's cache key is the one it was run with.  After a
change of the lifter the key changes and every chunk is run again (`status` shows them as
not yet run and says how many results of another lift are lying there).
"""
import argparse
import glob
import hashlib
import json
import math
import os
import resource
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "liftcache"))
sys.path.insert(0, os.path.join(REPO, "harness"))
sys.path.insert(0, REPO)

MANIFEST = os.path.join(REPO, "gate3", "modules.def")
CHUNK = 256                 # functions per chunk, on average
BATCH = 4                   # functions per round of oracle and candidate inside a chunk (memory)
SEEDS = 12                  # base seeds (make difftest-func's default); 3 of each family on top
FAMILY_SEEDS = 3
LARGE = ("engine", "client", "server")
SAMPLE = 0.10               # of each large module, before the rest of any of them
NICE = 10
NOT_PREPARED = 3            # exit status of `chunk`: the module's static directory is not of this lift and harness


def scratch():
    return os.path.abspath(os.environ.get("FUNCTEST_DIR") or os.path.join(REPO, "build", "functest"))


def mdir(module, *more):
    return os.path.join(scratch(), module, *more)


def modules():
    """[(module, dll path)] in boot order (gate3/modules.def)."""
    import liftcache
    return liftcache.read_manifest(MANIFEST)


def chunk_of(module, rva, n):
    """Which of a module's n chunks the function at `rva` is in."""
    h = hashlib.sha256(("functest:%s:%x" % (module, rva)).encode()).digest()
    return int.from_bytes(h[:8], "big") % n


def _digest(paths, *more):
    h = hashlib.sha256()
    for p in sorted(paths):
        with open(p, "rb") as f:
            h.update(os.path.relpath(p, REPO).encode() + b"\0" + f.read() + b"\0")
    for x in more:
        h.update(x.encode() + b"\0")
    return h.hexdigest()[:16]


def harness_digest():
    """What a chunk's verdicts depend on besides the lift: the harness.  Kept in the chunk's
    file, for the record; a chunk is not run again because it changed."""
    return _digest(glob.glob(os.path.join(REPO, "harness", "*.py")) + glob.glob(os.path.join(REPO, "harness", "*.[ch]"))
                   + glob.glob(os.path.join(REPO, "harness", "insns", "*.h")))


def static_digest():
    """What <module>/static depends on besides the lift: the driver's source and every header
    the lifted C includes, the flags, and how the memory files and image variants are made.  A
    module is prepared again when this changes."""
    import inspect
    import difftest
    return _digest(glob.glob(os.path.join(REPO, "harness", "*.[ch]")) + glob.glob(os.path.join(REPO, "harness", "insns", "*.h")),
                   repr(difftest.LIFTED_CFLAGS), inspect.getsource(difftest.make_mem),
                   inspect.getsource(difftest.zero_filled_image), inspect.getsource(difftest.Session.build_driver),
                   "seeds %d %d" % (SEEDS, FAMILY_SEEDS))


_KEYS = {}


def lift_entry(module, dll, fresh=False):
    """(cache key, entry directory or None) of the module's lift as this worktree's lifter,
    headers and the Gate 3 build's flags key it: the entry `make gate3-build` uses."""
    import liftcache
    if fresh:
        _KEYS.clear()
    if "inputs" not in _KEYS:
        _KEYS["inputs"] = liftcache.Inputs(liftcache.DEFAULT_CFLAGS, "xcrun clang")
        _KEYS["root"] = liftcache.cache_root(None)
    if module not in _KEYS:
        keys = _KEYS["inputs"].keys(module, dll)
        _KEYS[module] = (keys["key"], liftcache.find_hit(os.path.join(_KEYS["root"], module), keys))
    return _KEYS[module]


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.tmp.%d" % (path, os.getpid())
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=1)
    os.replace(tmp, path)


def prepared(module, dll):
    """The module's prepared.json if it is of the current lift and harness, else None."""
    p = read_json(mdir(module, "prepared.json"))
    if not p:
        return None
    key, entry = lift_entry(module, dll)
    if p.get("lift_key") != key or p.get("static") != static_digest() or not entry:
        return None
    return p


def chunk_path(module, i):
    return mdir(module, "chunks", "c%04d.json" % i)


def chunk_done(module, i, key):
    """True if the chunk's file is there and of this lift.  Reads only the head of the file."""
    try:
        with open(chunk_path(module, i)) as f:
            head = f.read(400)
    except OSError:
        return False
    return ('"lift_key": "%s"' % key) in head


# ---- prepare ---------------------------------------------------------------------------------------

def cmd_prepare(args):
    import difftest
    from lifter.image import Image
    dll = dict(modules())[args.module]
    key, entry = lift_entry(args.module, dll)
    if not entry:
        sys.exit("functest: the cache has no lift of %s for this worktree's lifter (key %s): run `make gate3-build`"
                 % (args.module, key[:16]))
    if prepared(args.module, dll) and not args.force:
        print("functest: %s is prepared (lift %s)" % (args.module, key[:16]))
        return 0
    t0 = time.time()
    digest = static_digest()        # before anything is compiled: an edit during the build must show as a mismatch later
    static = mdir(args.module, "static")
    if os.path.isdir(static):
        shutil.rmtree(static)
    try:
        os.remove(mdir(args.module, "prepared.json"))
    except OSError:
        pass
    image = Image.from_pe(dll, args.module)
    lifted = os.path.join(entry, "src")
    report = difftest.function_test_prepare(image, lifted, static, seeds=SEEDS, family_seeds=FAMILY_SEEDS, jobs=args.jobs)
    shutil.rmtree(os.path.join(static, "obj"), ignore_errors=True)      # linked; only the driver is used
    from lifter import lift as lifter_lift
    funcs = [r for r in report["functions"] if lifter_lift.is_lifted(r)]
    old = read_json(mdir(args.module, "plan.json"))
    n = old["nchunks"] if old else max(1, math.ceil(len(funcs) / CHUNK))
    if not old:
        # N is fixed for good: a function's chunk must not move when the count of lifted functions does
        write_json(mdir(args.module, "plan.json"), {"module": args.module, "nchunks": n, "chunk": CHUNK,
                                                    "lifted_when_fixed": len(funcs)})
    ru = resource.getrusage(resource.RUSAGE_CHILDREN)
    write_json(mdir(args.module, "prepared.json"), {
        "module": args.module, "lift_key": key, "entry": entry, "static": digest, "nchunks": n,
        "seeds": SEEDS, "family_seeds": FAMILY_SEEDS,
        "functions_found": len(report["functions"]), "lifted": len(funcs),
        "skipped": len(report["functions"]) - len(funcs),
        "guest_patches": {r["rva"]: r["guest_patches"] for r in funcs if r.get("guest_patches")},
        "rvas": [int(r["rva"], 16) for r in funcs],
        "prepare_seconds": round(time.time() - t0, 1), "prepare_cpu_seconds": round(ru.ru_utime + ru.ru_stime, 1),
        "static_bytes": sum(os.path.getsize(p) for p in glob.glob(os.path.join(static, "*")) if os.path.isfile(p))})
    print("functest: prepared %s: %d lifted functions in %d chunks, %.0f s (lift %s)"
          % (args.module, len(funcs), n, time.time() - t0, key[:16]))
    return 0


# ---- one chunk -------------------------------------------------------------------------------------

def cmd_chunk(args):
    import difftest
    from lifter.image import Image
    module, i = args.module, args.n
    dll = dict(modules())[module]
    p = prepared(module, dll)
    if not p:
        print("functest: %s is not prepared for the current lift and harness: functest.py prepare %s" % (module, module),
              file=sys.stderr)
        return NOT_PREPARED
    if not 0 <= i < p["nchunks"]:
        sys.exit("functest: %s has chunks 0 to %d" % (module, p["nchunks"] - 1))
    if chunk_done(module, i, p["lift_key"]) and not args.force:
        print("functest: %s chunk %d is done" % (module, i))
        return 0
    only = {rva for rva in p["rvas"] if chunk_of(module, rva, p["nchunks"]) == i}
    work = mdir(module, "work", "c%04d.%d" % (i, os.getpid()))
    t0 = time.time()
    image = Image.from_pe(dll, module)
    try:
        summary, results = difftest.function_test(
            image, os.path.join(p["entry"], "src"), mdir(module, "static"), seeds=SEEDS, only=only, jobs=1,
            quiet=True, family_seeds=FAMILY_SEEDS, work_dir=work, prepared=True, batch=BATCH)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    patched = p["guest_patches"]
    for r in results:
        if r["rva"] in patched:
            r["guest_patches"] = patched[r["rva"]]
    me, kids = resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(resource.RUSAGE_CHILDREN)
    # "lift_key" first: chunk_done reads the head of the file
    write_json(chunk_path(module, i), {
        "lift_key": p["lift_key"], "module": module, "chunk": i, "nchunks": p["nchunks"], "harness": harness_digest(),
        "finished": time.strftime("%Y-%m-%d %H:%M:%S"), "seconds": round(time.time() - t0, 1),
        "cpu_seconds": round(me.ru_utime + me.ru_stime + kids.ru_utime + kids.ru_stime, 1),
        "peak_rss_mb": round(me.ru_maxrss / 2 ** 20), "driver_peak_rss_mb": round(kids.ru_maxrss / 2 ** 20),
        "summary": summary, "functions": results})
    print("functest: %s chunk %d of %d: %d functions, passing %d, failing %d, inconclusive %d; %.0f s, peak %d MB"
          % (module, i, p["nchunks"], len(results), summary["passing"], summary["failing"], summary["inconclusive"],
             time.time() - t0, me.ru_maxrss / 2 ** 20))
    return 0


def cmd_one(args):
    """One function again, now, from the module's prepared directory; its whole record as JSON.
    For narrowing a failure: nothing is written to the chunk files."""
    import difftest
    from lifter.image import Image
    module = args.module
    dll = dict(modules())[module]
    p = prepared(module, dll)
    if not p:
        print("functest: %s is not prepared: functest.py prepare %s" % (module, module), file=sys.stderr)
        return NOT_PREPARED
    only = {int(x, 16) for x in args.rva}
    try:
        aim = difftest.parse_aim(args.reg) if args.reg else None
    except ValueError as e:
        print("functest: %s" % e, file=sys.stderr)
        return 2
    work = mdir(module, "work", "one.%d" % os.getpid())
    try:
        summary, results = difftest.function_test(
            Image.from_pe(dll, module), os.path.join(p["entry"], "src"), mdir(module, "static"), seeds=args.seeds,
            only=only, jobs=1, quiet=True, family_seeds=args.family_seeds, work_dir=work, prepared=True,
            paging=not args.no_paging, cut=not args.no_cut, watch={int(x, 16) for x in args.watch}, aim=aim)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for r in results:
        if not args.full:
            r.pop("inconclusive_cases", None)
        print(json.dumps(r, indent=1))
    return 1 if summary["failing"] else 0


# ---- reading results -------------------------------------------------------------------------------

def verdict(rec):
    """One of: passing, failing, inconclusive, stop (no verdict, and at least one seed ended, in
    agreement, at an instruction the lifter does not translate), patched-* (the function holds a
    guest patch, lifter/guest_patches.toml: the lifted code differs from the bytes the emulator
    runs on purpose, so its verdict is kept apart whatever it is)."""
    s = rec["status"]
    if rec.get("guest_patches"):
        return "patched-" + s
    if s == "inconclusive" and rec.get("stopped"):
        return "stop"
    return s


def chunks_of(module, key):
    """The chunk files of a module that are of lift `key`, and the count of those that are not."""
    good, other = [], 0
    for path in sorted(glob.glob(mdir(module, "chunks", "c*.json"))):
        c = read_json(path)
        if c and c.get("lift_key") == key:
            good.append(c)
        else:
            other += 1
    return good, other


def lifted_count(module, dll):
    """(lifted, skipped, nchunks, key) without preparing: from prepared.json, else the cache's report."""
    key, entry = lift_entry(module, dll)
    p = read_json(mdir(module, "prepared.json"))
    if p and p.get("lift_key") == key:
        return p["lifted"], p["skipped"], p["nchunks"], key
    plan = read_json(mdir(module, "plan.json"))
    if not entry:
        return None, None, plan and plan["nchunks"], key
    rep = read_json(os.path.join(entry, "src", module + ".report.json"))
    s = rep["summary"]
    return s["lifted"], s["skipped"], plan["nchunks"] if plan else max(1, math.ceil(s["lifted"] / CHUNK)), key


COLS = ("passing", "failing", "inconclusive", "stop")


def cmd_status(args):
    mods = modules()
    tot = dict.fromkeys(COLS + ("lifted", "run", "patched", "skipped"), 0)
    print("%-20s %7s %7s %7s %5s %6s %5s %7s %8s  %s" % ("module", "lifted", "run", "passing", "fail", "inconc",
                                                          "stop", "patched", "not run", "chunks"))
    notes, fails = [], []
    cpu = wall = 0.0
    for m, dll in mods:
        lifted, skipped, n, key = lifted_count(m, dll)
        if lifted is None:
            print("%-20s no lift in the cache for this worktree's lifter (make gate3-build)" % m)
            continue
        cs, other = chunks_of(m, key)
        c = dict.fromkeys(COLS, 0)
        pat = []
        for ch in cs:
            cpu += ch["cpu_seconds"]
            wall += ch["seconds"]
            for r in ch["functions"]:
                v = verdict(r)
                if v.startswith("patched-"):
                    pat.append("%s+%s %s (%d of %d seeds conclusive, %d agree)" % (m, r["rva"], v[8:], r["conclusive"],
                                                                                   r["seeds"], r["agree"]))
                else:
                    c[v] += 1
                if r["status"] == "failing":
                    fails.append("%s+%s" % (m, r["rva"]))
        run = sum(c.values()) + len(pat)
        print("%-20s %7d %7d %7d %5d %6d %5d %7d %8d  %d of %d%s" % (
            m, lifted, run, c["passing"], c["failing"], c["inconclusive"], c["stop"], len(pat), lifted - run, len(cs), n,
            ", %d of another lift" % other if other else ""))
        for k in COLS:
            tot[k] += c[k]
        tot["lifted"] += lifted
        tot["run"] += run
        tot["patched"] += len(pat)
        tot["skipped"] += skipped
        notes += pat
    print("%-20s %7d %7d %7d %5d %6d %5d %7d %8d" % ("all", tot["lifted"], tot["run"], tot["passing"], tot["failing"],
                                                    tot["inconclusive"], tot["stop"], tot["patched"], tot["lifted"] - tot["run"]))
    print("stop: no verdict, and at least one seed ended at an instruction the lifter does not translate, both sides "
          "in the same state there.  %d functions are not lifted at all (skipped) and cannot be run." % tot["skipped"])
    for x in notes:
        print("patched: " + x)
    if fails:
        print("failing: " + " ".join(fails[:40]) + (" ... (functest.py failures)" if len(fails) > 40 else ""))
    print("chunks so far: %.1f CPU-hours, %.1f hours of job time" % (cpu / 3600, wall / 3600))
    pid = running_pid()
    print("the detached run: %s" % ("running, pid %d (log: %s)" % (pid, os.path.join(scratch(), "run.log")) if pid
                                    else "not running (functest.py start)"))
    return 0


def cmd_failures(args):
    for m, dll in modules():
        if args.module and m not in args.module:
            continue
        key, _ = lift_entry(m, dll)
        for ch in chunks_of(m, key)[0]:
            for r in ch["functions"]:
                if r["status"] == "failing":
                    f = r["failures"][0]
                    print("%s+%s%s  chunk %d  %d of %d conclusive seeds agree; seed %s: %s" % (
                        m, r["rva"], " [guest patch]" if r.get("guest_patches") else "", ch["chunk"], r["agree"],
                        r["conclusive"], f["seed"], "; ".join(str(x) for x in f["mismatches"][:2])[:300]))
    return 0


SUMS = ("cases", "cases_conclusive", "cases_ending_in_a_defined_trap", "cases_ending_at_a_stop",
        "cases_ending_at_an_event_limit", "cases_run_again_with_the_probe_contract",
        "cases_conclusive_with_the_probe_contract", "cases_with_demand_pages", "demand_pages",
        "functions_executing_unlifted_code", "passing_on_base_seeds_alone")


def module_report(m, dll):
    import difftest
    lifted, skipped, n, key = lifted_count(m, dll)
    cs, other = chunks_of(m, key)
    recs = [r for ch in cs for r in ch["functions"]]
    by = {}
    for r in recs:
        by.setdefault(verdict(r), []).append(r)
    rep = {"module": m, "lift_key": key, "lifted": lifted, "skipped": skipped, "chunks": len(cs), "nchunks": n,
           "functions_run": len(recs), "not_yet_run": lifted - len(recs),
           "verdicts": {k: len(v) for k, v in sorted(by.items())},
           "functions_with_stops": sum(1 for r in recs if r.get("stops")),
           "functions_with_stops_passing": sum(1 for r in recs if r.get("stops") and r["status"] == "passing"),
           "coverage_all_run": difftest._coverage_totals(recs),
           "coverage_passing": difftest._coverage_totals([r for r in recs if r["status"] == "passing"]),
           "cpu_seconds": round(sum(ch["cpu_seconds"] for ch in cs)), "job_seconds": round(sum(ch["seconds"] for ch in cs)),
           "oracle_seconds": round(sum(ch["summary"]["oracle_seconds"] for ch in cs)),
           "candidate_seconds": round(sum(ch["summary"]["candidate_seconds"] for ch in cs)),
           "peak_rss_mb": max([ch["peak_rss_mb"] for ch in cs] or [0])}
    for k in SUMS:
        rep[k] = sum(ch["summary"][k] for ch in cs)
    why = {}
    for ch in cs:
        for k, v in ch["summary"]["inconclusive_functions_by_reason"].items():
            why[k] = why.get(k, 0) + v
    rep["inconclusive_functions_by_reason"] = dict(sorted(why.items(), key=lambda kv: -kv[1]))
    rep["failing"] = [{"rva": r["rva"], "agree": r["agree"], "conclusive": r["conclusive"], "failures": r["failures"]}
                      for r in recs if r["status"] == "failing"]
    rep["patched"] = [{"rva": r["rva"], "status": r["status"], "agree": r["agree"], "conclusive": r["conclusive"],
                       "seeds": r["seeds"]} for r in recs if r.get("guest_patches")]
    return rep


def cmd_report(args):
    import difftest
    for m, dll in modules():
        if args.module and m not in args.module:
            continue
        rep = module_report(m, dll)
        if not rep["chunks"]:
            continue
        write_json(mdir(m, "report.json"), rep)
        v = rep["verdicts"]
        print("%s: %d of %d lifted functions run (%d of %d chunks): %s" % (
            m, rep["functions_run"], rep["lifted"], rep["chunks"], rep["nchunks"],
            ", ".join("%s %d" % kv for kv in v.items())))
        print("  cases %d, conclusive %d; at a defined trap %d, at a stop %d, at an event limit %d; %d functions with "
              "stops, %d of them passing" % (rep["cases"], rep["cases_conclusive"], rep["cases_ending_in_a_defined_trap"],
                                             rep["cases_ending_at_a_stop"], rep["cases_ending_at_an_event_limit"],
                                             rep["functions_with_stops"], rep["functions_with_stops_passing"]))
        print(difftest._coverage_line("the %d functions run" % rep["functions_run"], rep["coverage_all_run"]))
        print(difftest._coverage_line("the passing functions", rep["coverage_passing"]))
        print("  cost: %.2f CPU-hours (oracle %d s, candidate %d s of job time), largest chunk process %d MB" % (
            rep["cpu_seconds"] / 3600, rep["oracle_seconds"], rep["candidate_seconds"], rep["peak_rss_mb"]))
        print("  inconclusive functions by reason: %s" % ", ".join(
            "%s %d" % kv for kv in list(rep["inconclusive_functions_by_reason"].items())[:8]))
    return 0


# ---- what the start of the game is known to run ----------------------------------------------------

def started_functions(log):
    """{module: {rva of a lifted function}}: the functions that a run's log shows running.  The
    log (build/gate3/run.log after `build/gate3/gate3 run`) names guest code only where a
    Windows function was called from (`ret=<module>.dll+0x<rva>`) and in the stack of the final
    report (`#n <module>.dll+0x<rva>`), so this is a LOWER BOUND: a function that ran and called
    nothing of Windows, and was not on the stack at the end, is not in it."""
    import bisect
    import re
    sites = {}
    pat = re.compile(r"(?:ret=|#\d+ )([A-Za-z0-9_]+)\.dll\+0x([0-9a-f]+)")
    with open(log, errors="replace") as f:
        for line in f:
            for m in pat.finditer(line):
                sites.setdefault(m.group(1).lower(), set()).add(int(m.group(2), 16))
    out = {}
    for m, dll in modules():
        if m not in sites:
            continue
        entry = lift_entry(m, dll)[1]
        rep = entry and read_json(os.path.join(entry, "src", m + ".report.json"))
        if not rep:
            continue
        spans = sorted((a, b, int(r["rva"], 16)) for r in rep["functions"] if r["status"].startswith("lifted")
                       for a, b in r["ranges"])
        starts = [a for a, _, _ in spans]
        got = set()
        for rva in sites[m]:
            # a return address is the byte after the call: it may be the first byte past the range
            i = bisect.bisect_right(starts, rva - 1) - 1
            if i >= 0 and spans[i][0] <= rva - 1 < spans[i][1]:
                got.add(spans[i][2])
        out[m] = got
    return out


def cmd_started(args):
    whole = None if args.from_log else read_json(os.path.join(scratch(), "cov", "ran.json"))
    if whole:
        # tools/functest/startcov.py: every lifted function entered in runs of an instrumented gate3
        ran = {m: set(v) for m, v in whole.items()}
        src = "the instrumented start (build/functest/cov/ran.json, tools/functest/startcov.py) entered; all of them"
    else:
        ran = started_functions(args.log)
        src = "%s shows running (a lower bound: see started_functions)" % os.path.relpath(args.log, REPO)
    mods = dict(modules())
    verdicts = {}
    for m in ran:
        for ch in chunks_of(m, lift_entry(m, mods[m])[0])[0]:
            for r in ch["functions"]:
                verdicts[(m, int(r["rva"], 16))] = verdict(r)
    print("functions that %s:" % src)
    tot = {}
    for m, _ in modules():
        if m not in ran:
            continue
        c = {}
        for rva in ran[m]:
            v = verdicts.get((m, rva), "not yet run")
            c[v] = c.get(v, 0) + 1
            tot[v] = tot.get(v, 0) + 1
        print("  %-20s %5d: %s" % (m, len(ran[m]), ", ".join("%s %d" % kv for kv in sorted(c.items()))))
    print("  %-20s %5d: %s" % ("all", sum(tot.values()), ", ".join("%s %d" % kv for kv in sorted(tot.items()))))
    for x in args.function:
        m, rva = x.split("+")
        print("%s: %s" % (x, "the start runs it" if int(rva, 16) in ran.get(m, ()) else
                          "the start does not run it" if whole else
                          "not in the log (it may still run: only callers of Windows functions are there)"))
    if args.list:
        for m in ran:
            for rva in sorted(ran[m]):
                v = verdicts.get((m, rva), "not yet run")
                if args.list == "all" or v == args.list:
                    print("%s+0x%x %s" % (m, rva, v))
    return 0


# ---- the plan and the scheduler --------------------------------------------------------------------

def job_order():
    """[(module, chunk)]: the 16 smaller modules whole, in boot order; then the sample of each
    large module (its first SAMPLE of chunks); then the rest of engine; then the rest of client
    and server, turn about."""
    mods = modules()
    n = {m: lifted_count(m, dll)[2] for m, dll in mods}
    out = [(m, i) for m, _ in mods if m not in LARGE for i in range(n[m])]
    cut = {m: math.ceil(SAMPLE * n[m]) for m in LARGE}
    for m in LARGE:
        out += [(m, i) for i in range(cut[m])]
    out += [("engine", i) for i in range(cut["engine"], n["engine"])]
    rest = [[(m, i) for i in range(cut[m], n[m])] for m in ("client", "server")]
    for k in range(max(len(r) for r in rest)):
        out += [r[k] for r in rest if k < len(r)]
    return out


def cmd_plan(args):
    mods = dict(modules())
    order = job_order()
    left = [(m, i) for m, i in order if not chunk_done(m, i, lift_entry(m, mods[m])[0])]
    print("%d chunks in the plan, %d left" % (len(order), len(left)))
    runs, prev = [], None
    for m, i in left:
        if prev and prev[0] == m and prev[2] == i - 1:
            prev[2] = i
        else:
            prev = [m, i, i]
            runs.append(prev)
    for m, a, b in runs[:60]:
        print("  %-20s chunks %d to %d" % (m, a, b))
    return 0


def running_pid():
    try:
        pid = int(open(os.path.join(scratch(), "run.pid")).read().split()[0])
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError, IndexError):
        return None


def cmd_run(args):
    os.makedirs(os.path.join(scratch(), "log"), exist_ok=True)
    if running_pid() not in (None, os.getpid()):
        sys.exit("functest: a run is going (pid %d); functest.py stop" % running_pid())
    with open(os.path.join(scratch(), "run.pid"), "w") as f:
        f.write("%d\n" % os.getpid())
    try:
        os.nice(NICE)
    except OSError:
        pass
    log = open(os.path.join(scratch(), "run.log"), "a", buffering=1)

    def say(text):
        log.write("%s %s\n" % (time.strftime("%m-%d %H:%M:%S"), text))

    mods = dict(modules())
    order = job_order()
    if args.modules:
        order = [j for j in order if j[0] in args.modules]
    key = {m: lift_entry(m, mods[m])[0] for m in mods}
    todo = [j for j in order if not chunk_done(j[0], j[1], key[j[0]])]
    say("run starts: pid %d, %d jobs at once, %d chunks in the plan, %d to do" % (os.getpid(), args.jobs, len(order), len(todo)))
    ready = {m for m in mods if prepared(m, mods[m])}
    running = {}                # Popen -> (kind, module, chunk, t0)
    tries = {}
    broken = set()              # modules whose prepare failed, chunks that failed twice
    stopping, draining = [], []

    def on_term(sig, _):
        stopping.append(sig)
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)
    signal.signal(signal.SIGUSR1, lambda sig, _: draining.append(sig))      # stop --drain

    def spawn(kind, m, i=None):
        name = "%s.prepare" % m if kind == "prepare" else "%s.c%04d" % (m, i)
        cmd = [sys.executable, os.path.abspath(__file__), kind, m] + ([str(i)] if kind == "chunk" else ["--jobs", "1"])
        out = open(os.path.join(scratch(), "log", name + ".log"), "a")
        p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, cwd=REPO)
        out.close()
        running[p] = (kind, m, i, time.time())

    while not stopping:
        for p in [p for p in running if p.poll() is not None]:
            kind, m, i, t0 = running.pop(p)
            what = "prepare %s" % m if kind == "prepare" else "%s chunk %d" % (m, i)
            if p.returncode == 0:
                if kind == "prepare":
                    ready.add(m)
                else:
                    c = read_json(chunk_path(m, i)) or {}
                    s = c.get("summary", {})
                    what += ": passing %s, failing %s, inconclusive %s" % (s.get("passing"), s.get("failing"), s.get("inconclusive"))
                say("done   %s (%.0f s)" % (what, time.time() - t0))
            elif kind == "chunk" and p.returncode == NOT_PREPARED:
                # the harness or the lift changed under the run: prepare the module again, then the chunk
                ready.discard(m)
                key[m] = lift_entry(m, mods[m], fresh=True)[0]
                say("again  %s: %s is no longer prepared" % (what, m))
            else:
                tries[(kind, m, i)] = tries.get((kind, m, i), 0) + 1
                again = tries[(kind, m, i)] < 2
                say("FAILED %s, status %d (%.0f s); %s; see log/" % (what, p.returncode, time.time() - t0,
                                                                     "once more" if again else "given up"))
                if not again:
                    broken.add(m if kind == "prepare" else (m, i))
        active = {(k, m, i) for k, m, i, _ in running.values()}
        todo = [j for j in todo if not chunk_done(j[0], j[1], key[j[0]]) and j not in broken and j[0] not in broken]
        if (not todo or draining) and not running:
            break
        if draining:
            time.sleep(1.0)
            continue
        # one prepare at a time, for the first module in the order that is not prepared: it runs
        # ahead of the chunks, which take the other places
        if len(running) < args.jobs and not any(k == "prepare" for k, _, _ in active):
            nxt = next((m for m, _ in todo if m not in ready), None)
            if nxt:
                spawn("prepare", nxt)
                say("start  prepare %s" % nxt)
                continue
        for m, i in todo:
            if len(running) >= args.jobs:
                break
            if m in ready and ("chunk", m, i) not in active:
                spawn("chunk", m, i)
                active.add(("chunk", m, i))
        time.sleep(1.0)

    if stopping:
        say("stopping on signal %d: %d jobs killed" % (stopping[0], len(running)))
        for p in running:
            p.terminate()
        for p in running:
            p.wait()
    else:
        say("run ends: %s%s" % ("drained on request" if draining else "nothing left to do",
                                "; given up on: %s" % sorted(map(str, broken)) if broken else ""))
    try:
        os.remove(os.path.join(scratch(), "run.pid"))
    except OSError:
        pass
    return 0


def cmd_start(args):
    os.makedirs(os.path.join(scratch(), "log"), exist_ok=True)
    if running_pid():
        sys.exit("functest: a run is going (pid %d)" % running_pid())
    cmd = [sys.executable, os.path.abspath(__file__), "run", "--jobs", str(args.jobs)]
    if args.modules:
        cmd += ["--modules"] + args.modules
    out = open(os.path.join(scratch(), "log", "scheduler.out"), "a")
    p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, cwd=REPO,
                         start_new_session=True)
    print("functest: started, pid %d; state: %s status; log: %s" % (
        p.pid, os.path.relpath(os.path.abspath(__file__), REPO), os.path.join(scratch(), "run.log")))
    return 0


def cmd_stop(args):
    pid = running_pid()
    if not pid:
        print("functest: no run is going")
        return 0
    if args.drain:
        os.kill(pid, signal.SIGUSR1)
        print("functest: pid %d starts no more jobs and ends when those running have (tail %s)"
              % (pid, os.path.join(scratch(), "run.log")))
        return 0
    os.kill(pid, signal.SIGTERM)
    for _ in range(100):
        time.sleep(0.2)
        if not running_pid():
            break
    print("functest: stopped pid %d" % pid if not running_pid() else "functest: pid %d has not ended yet" % pid)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("start", "run"):
        p = sub.add_parser(name)
        p.add_argument("--jobs", type=int, default=4, help="processes at once (default 4)")
        p.add_argument("--modules", nargs="+", help="only these modules of the plan")
    for name in ("status", "plan"):
        sub.add_parser(name)
    sub.add_parser("stop").add_argument("--drain", action="store_true",
                                        help="let the running jobs finish instead of killing them")
    for name in ("failures", "report"):
        sub.add_parser(name).add_argument("module", nargs="*")
    p = sub.add_parser("prepare")
    p.add_argument("module")
    p.add_argument("--jobs", type=int, default=2, help="compiler processes (default 2)")
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("started")
    p.add_argument("function", nargs="*", help="module+0xrva: is this one in the log?")
    p.add_argument("--log", default=os.path.join(REPO, "build", "gate3", "run.log"))
    p.add_argument("--list", help="also list the functions with this verdict (or `all`)")
    p.add_argument("--from-log", action="store_true", help="use the run's log even when startcov.py's list is there")
    p = sub.add_parser("one")
    p.add_argument("module")
    p.add_argument("rva", nargs="+", help="function RVAs (hex)")
    p.add_argument("--seeds", type=int, default=SEEDS)
    p.add_argument("--family-seeds", type=int, default=FAMILY_SEEDS)
    p.add_argument("--no-paging", action="store_true")
    p.add_argument("--no-cut", action="store_true")
    p.add_argument("--full", action="store_true", help="with the list of inconclusive cases")
    p.add_argument("--watch", nargs="*", default=[], help="instruction RVAs: in how many conclusive seeds each was executed")
    p.add_argument("--reg", action="append", default=[], metavar="REG=V[,V...]",
                   help="set this register at entry (rcx=7, edx=0,1,2), or the memory it points at ([rcx+8]:32=5); "
                        "every seed runs once per combination")
    p = sub.add_parser("chunk")
    p.add_argument("module")
    p.add_argument("n", type=int)
    p.add_argument("--force", action="store_true")
    args = ap.parse_args()
    return globals()["cmd_" + args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
