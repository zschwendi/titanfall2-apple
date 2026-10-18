#!/usr/bin/env python3
"""How often a run of `gate3 run` hangs at the start of the client DLL, with and without load.

    python3 tools/gate3/hang_rate.py -n 40                      40 runs, the machine as it is
    python3 tools/gate3/hang_rate.py -n 40 --load 22            the same under 22 busy loops
    python3 tools/gate3/hang_rate.py -n 40 --load 22 --nice 0   ... that are not niced
    python3 tools/gate3/hang_rate.py -n 5 --keep-all --out DIR  keep every run's files

One run: the driver with a time limit (--timeout, GATE3_TIMEOUT), its log polled four times a
second for THE MARKER, the first line the MAIN thread writes after the client's start: the
engine asking the client DLL for its interface "GameUI011" (a line of the driver's own default
probe on client.dll's CreateInterface).  The client's start runs on the job worker, whose last
lines are its requests for "VClientPrediction001" and "VClientEntityList003"; a hung run has
been seen to stop before the first of them and after the second, so neither is the marker.
(The log is written through a buffer: the marker is seen when the buffer that holds it is
written, a moment after the fact.)  The run that writes it is ended at once (SIGKILL to the
run's own process, the driver's child; nothing else is ever signalled) and counts as PASSED.
A run that reaches the time limit without it counts as HUNG when its log has the line that a
run entering the client's start writes (--entered: the client's interface factory asked for
"EventSystem001"), else as SLOW (it never got as far: raise --timeout).  A run that ends in
another way (a stop, a crash) is OTHER.  The files of a hung run are kept (log, stderr, the
driver's output with the time limit's report of every thread's wait), the others deleted.
--args "<game arguments>" replaces the driver's default command line (gate3/run.c,
run_default_args), for trying another number of job workers.
--sample S runs `sample <pid> 2` on a run that has not written the marker S seconds before
its time limit: the host stacks of a hung run, in <prefix>.sample.

THE LOAD is the script's own: --load K starts K processes that spin (`nice -n 19` unless
--nice says otherwise), each watching for the script's death, and stops them when the runs
are done or the script is interrupted.  It makes every other run of the game on this machine
slow and, before the fix, hang: measure in short bursts.

The script reads the runtime's log and the driver's output only: no game file.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time

EXTS = (".log", ".stderr", ".out", ".sample")
MARKER = b'"GameUI011"'
ENTERED = b'"EventSystem001"'

SPIN = ("import os,sys\n"
        "pp=int(sys.argv[1])\n"
        "while os.getppid()==pp:\n"
        "    for _ in range(2000000): pass\n")


def start_load(k, nice):
    cmd = [sys.executable, "-c", SPIN, str(os.getpid())]
    if nice:
        cmd = ["nice", "-n", str(nice)] + cmd
    return [subprocess.Popen(cmd, stdin=subprocess.DEVNULL) for _ in range(k)]


def stop_load(procs):
    for p in procs:
        p.terminate()
    for p in procs:
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()


def children_of(pid):
    r = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True)
    return [int(x) for x in r.stdout.split()]


def log_has(path, needle, state):
    """True once `needle` is in the file; reads only what was appended since the last call."""
    try:
        with open(path, "rb") as f:
            f.seek(max(0, state["at"] - len(needle)))
            data = f.read()
            state["at"] = f.tell()
    except FileNotFoundError:
        return False
    return needle in data


def one_run(binary, prefix, timeout, env_extra, sample_before, game_args):
    env = dict(os.environ, GATE3_TIMEOUT=str(timeout), GATE3_RUN_OUT=prefix)
    env.update(env_extra)
    log = prefix + ".log"
    for ext in EXTS:
        try:
            os.unlink(prefix + ext)
        except FileNotFoundError:
            pass
    t0 = time.time()
    sampled = not sample_before
    with open(prefix + ".out", "wb") as out:
        drv = subprocess.Popen([binary, "run"] + (["--"] + game_args if game_args else []), stdout=out, stderr=subprocess.STDOUT, env=env, stdin=subprocess.DEVNULL)
    state = {"at": 0}
    seen = False
    while drv.poll() is None:
        if log_has(log, MARKER, state):
            seen = True
            for c in children_of(drv.pid):          # the run's own process, nothing else
                try:
                    os.kill(c, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            break
        if not sampled and time.time() - t0 > timeout - sample_before:
            sampled = True
            for c in children_of(drv.pid):
                subprocess.run(["sample", str(c), "2", "-file", prefix + ".sample"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.25)
    drv.wait()
    took = time.time() - t0
    if seen:
        return "passed", took
    with open(prefix + ".out", "rb") as f:
        out = f.read()
    if b"NO END after" not in out:
        return "other", took
    try:
        with open(log, "rb") as f:
            entered = ENTERED in f.read()
    except FileNotFoundError:
        entered = False
    return ("hung" if entered else "slow"), took


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-n", type=int, default=40, help="runs (40)")
    ap.add_argument("--load", type=int, default=0, help="busy loops to run beside the runs (0)")
    ap.add_argument("--nice", type=int, default=19, help="their niceness (19; 0: not niced)")
    ap.add_argument("--timeout", type=int, default=45, help="the driver's time limit per run, seconds (45)")
    ap.add_argument("--bin", default="build/gate3/gate3", help="the driver (build/gate3/gate3)")
    ap.add_argument("--out", default="build/gate3/hang", help="where the runs' files go (build/gate3/hang)")
    ap.add_argument("--tag", default="r", help="prefix of the runs' file names (r)")
    ap.add_argument("--keep-all", action="store_true", help="keep the files of every run, not only of hung ones")
    ap.add_argument("--stop-at-hang", type=int, default=0, help="stop after this many hung runs (0: never)")
    ap.add_argument("--sample", type=int, default=0, help="sample a run with no marker this many seconds before its time limit (0: never)")
    ap.add_argument("--args", default="", help="the game's command line in place of the driver's default one (one string, split at spaces)")
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE for the runs (repeatable)")
    a = ap.parse_args()

    if not os.access(a.bin, os.X_OK):
        sys.exit(f"hang_rate: no driver at {a.bin}: make gate3-build")
    os.makedirs(a.out, exist_ok=True)
    env_extra = dict(e.split("=", 1) for e in a.env)
    load = start_load(a.load, a.nice) if a.load else []

    def bye(sig, frame):
        stop_load(load)
        sys.exit(130)
    signal.signal(signal.SIGINT, bye)
    signal.signal(signal.SIGTERM, bye)

    counts = {"passed": 0, "hung": 0, "slow": 0, "other": 0}
    times = []
    try:
        if load:
            time.sleep(3)                           # let the load reach every core
        for i in range(1, a.n + 1):
            prefix = os.path.join(a.out, f"{a.tag}{i:03d}")
            what, took = one_run(a.bin, prefix, a.timeout, env_extra, a.sample, a.args.split())
            counts[what] += 1
            if what == "passed":
                times.append(took)
            la = os.getloadavg()[0]
            print(f"run {i:3d}: {what:6s} {took:6.1f} s  load {la:5.1f}" + (f"  kept {prefix}.*" if what != "passed" or a.keep_all else ""), flush=True)
            if what == "passed" and not a.keep_all:
                for ext in EXTS:
                    try:
                        os.unlink(prefix + ext)
                    except FileNotFoundError:
                        pass
            if a.stop_at_hang and counts["hung"] >= a.stop_at_hang:
                break
    finally:
        stop_load(load)
    n = sum(counts.values())
    t = f"; a passed run took {min(times):.1f} to {max(times):.1f} s to the marker" if times else ""
    print(f"hang_rate: {counts['hung']} of {n} runs hung at the client's start ({counts['passed']} passed, {counts['slow']} slow, "
          f"{counts['other']} other); load: {a.load} busy loops" + (f" at nice {a.nice}" if a.load else "") + f"; time limit {a.timeout} s{t}")
    shutil.rmtree(a.out, ignore_errors=True) if not os.listdir(a.out) else None
    return 0


if __name__ == "__main__":
    sys.exit(main())
