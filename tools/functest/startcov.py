#!/usr/bin/env python3
"""Which lifted functions the start of the game runs: all of them, not only those the run's
log names (functest.py started without this sees the callers of Windows functions only).

    startcov.py build [--jobs 3]   a second gate3 under <scratch>/cov: the same lifted C, compiled
                                   with -finstrument-functions into a cache of its
                                   own (<scratch>/covcache, the current entries cloned in so that
                                   nothing is lifted again), linked with covhook.c
    startcov.py run [N]            `gate3 run` of it N times (default 3); every function entered
                                   is marked in <scratch>/cov/map.bin, which adds up over runs
    startcov.py collect            <scratch>/cov/ran.json: {module: [rva, ...]}; functest.py
                                   started reads it when it is there

It needs build/gate3/gate3 built (make gate3-build): the driver's objects and librt.a are taken
from there.  The run's log and state: <scratch>/cov/run.log, and build/gate3/state as usual.
The entries in covcache are read-only like the cache's: chmod -R u+w before deleting."""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import functest                                           # noqa: E402

REPO = functest.REPO
FLAG = "-finstrument-functions"


def cov(*more):
    return os.path.join(functest.scratch(), "cov", *more)


def sh(cmd, **kw):
    print("  " + " ".join(cmd)[:160], flush=True)
    subprocess.run(cmd, check=True, cwd=REPO, **kw)


def build(jobs):
    import liftcache
    cache = os.path.join(functest.scratch(), "covcache")
    for m, dll in functest.modules():
        key, entry = functest.lift_entry(m, dll)
        if not entry:
            sys.exit("startcov: no lift of %s in the cache (make gate3-build)" % m)
        dst = os.path.join(cache, m, os.path.basename(entry))
        if not os.path.isdir(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            subprocess.run(["cp", "-Rc", entry, dst], check=True)      # an APFS clone: no bytes copied
    os.makedirs(cov("lifted"), exist_ok=True)
    sh(["nice", "-n", "10", sys.executable, "tools/liftcache/liftcache.py", "ensure", "--manifest", functest.MANIFEST,
        "--cflags", liftcache.DEFAULT_CFLAGS + " " + FLAG, "--cache", cache, "--out", cov("lifted"), "--jobs", str(jobs)])
    sh(["xcrun", "clang", "-O2", "-c", os.path.join(HERE, "covhook.c"), "-o", cov("covhook.o")])
    objs = [os.path.join(d, f) for d, _, fs in os.walk(os.path.join(REPO, "build", "gate3", "obj")) for f in fs
            if f.endswith(".o")]
    sh(["xcrun", "clang", "-o", cov("gate3")] + sorted(objs) + ["-Wl,-filelist," + cov("lifted", "objects.filelist"),
        os.path.join(REPO, "build", "runtime", "librt.a"), cov("covhook.o")])


def run(n):
    env = dict(os.environ, COVHOOK_OUT=cov("map.bin"), GATE3_RUN_OUT=cov("run"))
    for k in range(n):
        r = subprocess.run([cov("gate3"), "run"], cwd=REPO, env=env, capture_output=True, text=True)
        last = [ln for ln in r.stdout.splitlines() if "step" in ln.lower()][:2]
        print("  run %d: status %d; %s" % (k + 1, r.returncode, " | ".join(x.strip()[:110] for x in last)))


def collect():
    with open(cov("map.bin"), "rb") as f:
        m = f.read()
    mods = sorted((x for x, _ in functest.modules()), key=len, reverse=True)
    out = {x: [] for x in mods}
    r = subprocess.run(["nm", cov("gate3")], capture_output=True, text=True, check=True)
    for ln in r.stdout.splitlines():
        f = ln.split()
        if len(f) != 3 or f[1] not in "Tt":
            continue
        i = (int(f[0], 16) - 0x100000000) >> 2
        if not (0 <= i < len(m) and m[i]):
            continue
        for x in mods:
            if f[2].startswith("_" + x + "_"):
                try:
                    out[x].append(int(f[2][len(x) + 2:], 16))
                except ValueError:
                    pass
                break
    out = {x: sorted(v) for x, v in out.items() if v}
    functest.write_json(cov("ran.json"), out)
    print("startcov: %d lifted functions entered: %s" % (sum(len(v) for v in out.values()),
                                                         ", ".join("%s %d" % (x, len(out[x])) for x, _ in functest.modules() if x in out)))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["build"]:
        build(int(a[a.index("--jobs") + 1]) if "--jobs" in a else 3)
    elif a[:1] == ["run"]:
        run(int(a[1]) if len(a) > 1 else 3)
    elif a[:1] == ["collect"]:
        collect()
    else:
        sys.exit(__doc__)
