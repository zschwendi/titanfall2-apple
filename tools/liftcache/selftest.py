#!/usr/bin/env python3
"""Self-test of tools/liftcache/liftcache.py.  No game file: it lifts and compiles the test
corpus (tests/corpus/build/corpus_O1.dll) into a cache of its own under harness/out.

    .venv/bin/python tools/liftcache/selftest.py          (or: make liftcache-test; part of make test)

What must hold: a second `ensure` is a hit and rewrites nothing; other flags are another
entry that reuses the lifted C; a changed lifter that writes the same C lifts again and
compiles nothing; two builders of one key at once leave one whole entry and nothing else;
entries are read-only; prune keeps the entries used last; a lift without the guest patches
(lifter/guest_patches.py) is an entry of its own, and the table of patches is part of every key.
"""

import glob
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(HERE, "liftcache.py")
DLL = os.path.join("tests", "corpus", "build", "corpus_O1.dll")
OUT = os.path.join(REPO, "harness", "out", "liftcache-selftest")
FAILS = []


def check(ok, what):
    if not ok:
        FAILS.append(what)
        print("  FAIL: " + what)


def tool(*args, env=None):
    e = dict(os.environ, **(env or {}))
    e.pop("LIFT_CACHE", None)
    return subprocess.run([sys.executable, TOOL] + list(args), cwd=REPO, env=e, capture_output=True, text=True)


def ensure(out, cflags=None, env=None, extra=()):
    args = ["ensure", "--manifest", os.path.join(OUT, "modules.def"), "--cache", os.path.join(OUT, "cache"),
            "--out", os.path.join(OUT, out), "--jobs", "4"] + list(extra)
    return tool(*(args + (["--cflags", cflags] if cflags else [])), env=env)


def entry_of(out):
    """The cache entry an `ensure --out` directory points at, and its stats."""
    with open(os.path.join(OUT, out, "keys.txt")) as f:
        entry = f.read().split()[2]
    with open(os.path.join(entry, "stats.json")) as f:
        return entry, json.load(f)


def main():
    t0 = time.time()
    if not os.path.exists(os.path.join(REPO, DLL)):
        print("liftcache selftest: SKIPPED: no %s (tests/corpus/build.sh makes it)" % DLL)
        return 0
    if os.path.isdir(OUT):                          # what an interrupted run left: entries are read-only
        subprocess.run(["chmod", "-R", "u+w", OUT])
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    with open(os.path.join(OUT, "modules.def"), "w") as f:
        f.write("# liftcache selftest\nlifted corpus_O1.dll dll=%s\nsystem kernel32.dll\n" % DLL)
    flags = ("-O2 -g0 -fno-strict-aliasing -fwrapv "
             "-mllvm -capture-tracking-max-uses-to-explore=1000000 -mllvm -dse-memoryssa-scanlimit=1500 "
             "-mllvm -dse-memoryssa-walklimit=1500 -mllvm -dse-memoryssa-path-check-limit=300 "
             "-mllvm -memssa-check-limit=1500 -mllvm -memdep-block-scan-limit=2000 "
             "-mllvm -memdep-block-number-limit=2000 -Iharness")

    # 1. a miss builds an entry; the list names objects that exist; the entry is read-only
    r = ensure("a")
    check(r.returncode == 0 and "1 built" in r.stdout, "the first ensure builds the module: %s%s" % (r.stdout[-300:], r.stderr[-300:]))
    if r.returncode != 0:
        return 1
    entry, st = entry_of("a")
    with open(os.path.join(OUT, "a", "objects.filelist")) as f:
        objs = f.read().split()
    check(objs and all(os.path.exists(o) for o in objs), "objects.filelist names objects that exist")
    check(st["tus"] == len(objs) == st["compiled"] and st["reused"] == 0 and not st["lift_reused"], "a first build compiles every file")
    check(os.path.isfile(os.path.join(OUT, "a", "corpus_o1", "corpus_o1.report.json")), "<out>/<module> links to the lifted C")
    check(not os.access(objs[0], os.W_OK) and not os.access(os.path.join(entry, "KEY.json"), os.W_OK), "the entry's files are read-only")

    # 2. the same again: a hit, and the list is not rewritten
    before = os.stat(os.path.join(OUT, "a", "objects.filelist")).st_mtime_ns
    r = ensure("a")
    check(r.returncode == 0 and "1 from the cache, 0 built" in r.stdout and "nothing changed" in r.stdout, "the second ensure is a hit")
    check(os.stat(os.path.join(OUT, "a", "objects.filelist")).st_mtime_ns == before, "a hit does not rewrite objects.filelist")

    # 3. other flags: another entry, the lifted C reused, everything compiled
    r = ensure("b", flags + " -DLIFTCACHE_SELFTEST=1")
    entry_b, st = entry_of("b")
    check(r.returncode == 0 and entry_b != entry, "other flags give another entry")
    check(st["lift_reused"] and st["compiled"] == st["tus"], "other flags reuse the lifted C and compile every file")

    # 4. a lifter that changed and writes the same C: lifted again, nothing compiled
    r = ensure("c", env={"LIFT_CACHE_SALT": "selftest"})
    entry_c, st = entry_of("c")
    check(r.returncode == 0 and entry_c not in (entry, entry_b), "a changed lifter gives another entry")
    check(not st["lift_reused"] and st["compiled"] == 0 and st["reused"] == st["tus"], "the same C from a changed lifter compiles nothing")
    check(os.path.samefile(os.path.join(entry_c, "obj", os.path.basename(objs[0])), objs[0]), "a reused object is a hard link, not a copy")

    # 5. two builders of one key at once: both succeed, one entry, nothing left behind
    cmd = [sys.executable, TOOL, "ensure", "--manifest", os.path.join(OUT, "modules.def"), "--cache", os.path.join(OUT, "cache"),
           "--jobs", "2", "--cflags", flags + " -DLIFTCACHE_SELFTEST=2"]
    env = dict(os.environ)
    env.pop("LIFT_CACHE", None)
    procs = [subprocess.Popen(cmd + ["--out", os.path.join(OUT, d)], cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
             for d in ("d1", "d2")]
    outs = [p.communicate()[0] for p in procs]
    check(all(p.returncode == 0 for p in procs), "two builders of one key both succeed: %s" % " / ".join(o[-200:] for o in outs))
    check(sum("1 built" in o for o in outs) == 1 and sum("1 from the cache" in o for o in outs) == 1,
          "of two builders of one key, one builds and the other uses what it built")
    check(entry_of("d1")[0] == entry_of("d2")[0], "both got the same entry")
    mod = os.path.join(OUT, "cache", "corpus_o1")            # the module name is the library id: lower case
    check(not glob.glob(os.path.join(mod, "tmp.*")), "no temporary directory is left")
    check(len(glob.glob(os.path.join(mod, "[0-9a-f]" * 16))) == 4, "four keys, four entries")

    # 6. prune keeps the two entries used last
    now = time.time()
    for age, e in ((300, entry), (200, entry_b), (100, entry_c)):
        os.utime(os.path.join(e, "USED"), (now - age, now - age))
    r = tool("prune", "--cache", os.path.join(OUT, "cache"), "--keep", "2")
    left = set(glob.glob(os.path.join(mod, "[0-9a-f]" * 16)))
    check(r.returncode == 0 and left == {entry_c, entry_of("d1")[0]}, "prune --keep 2 keeps the two entries used last: %s" % r.stdout[-300:])
    check(all(os.path.exists(os.path.join(entry_c, "obj", os.path.basename(o))) for o in objs),
          "an entry that stays keeps the objects it shared with one that went")
    r = ensure("c", env={"LIFT_CACHE_SALT": "selftest"})
    check(r.returncode == 0 and "1 from the cache" in r.stdout, "an entry that stayed is still a hit")
    r = ensure("a")
    check(r.returncode == 0 and "1 built" in r.stdout and entry_of("a")[1]["compiled"] == 0, "a pruned entry is built again, from the objects that stayed")

    # 7. without the guest patches: an entry of its own, lifted with --no-patches; the corpus
    #    has no patch, so the C is the same and nothing is compiled
    entry_a = entry_of("a")[0]
    r = ensure("e", extra=["--no-patches"])
    entry_e, st = entry_of("e")
    check(r.returncode == 0 and entry_e != entry_a, "--no-patches gives another entry")
    check(not st["lift_reused"] and st["compiled"] == 0 and st["reused"] == st["tus"], "which is lifted, not copied, and here compiles nothing")
    with open(os.path.join(entry_e, "KEY.json")) as f:
        key_e = json.load(f)
    with open(os.path.join(entry_e, "src", "corpus_o1.report.json")) as f:
        off = json.load(f)["summary"]["guest_patches"]
    with open(os.path.join(entry_a, "src", "corpus_o1.report.json")) as f:
        on = json.load(f)["summary"]["guest_patches"]
    check(off["enabled"] is False and on["enabled"] is True and key_e["guest_patches"].startswith("none"),
          "the lifter was run without patches for it (its report says so), and with them for the default entry")
    r = tool("key", "--manifest", os.path.join(OUT, "modules.def"), "--cache", os.path.join(OUT, "cache"), "--no-patches")
    check(r.returncode == 0 and key_e["key"][:16] in r.stdout and "hit" in r.stdout, "`key --no-patches` names that entry")

    # 8. the table of guest patches is part of every key
    sys.path.insert(0, HERE)
    import liftcache
    copy = os.path.join(OUT, "guest_patches.toml")
    shutil.copyfile(liftcache.GUEST_PATCH_TABLE, copy)
    real, liftcache.GUEST_PATCH_TABLE = liftcache.GUEST_PATCH_TABLE, copy
    try:
        k1 = liftcache.Inputs(flags, "xcrun clang").keys("corpus_o1", os.path.join(REPO, DLL))
        k2 = liftcache.Inputs(flags, "xcrun clang").keys("corpus_o1", os.path.join(REPO, DLL))
        with open(copy, "a") as f:
            f.write("\n# one more line\n")
        k3 = liftcache.Inputs(flags, "xcrun clang").keys("corpus_o1", os.path.join(REPO, DLL))
    finally:
        liftcache.GUEST_PATCH_TABLE = real
    check(k1["key"] == k2["key"] and k1["lift_key"] != k3["lift_key"] and k1["key"] != k3["key"],
          "a change of the table's bytes changes the key of a module, though the table lists nothing for it")

    subprocess.run(["chmod", "-R", "u+w", OUT])
    shutil.rmtree(OUT, ignore_errors=True)
    print("liftcache selftest: %s: %d checks failed (%.0f s)" % ("FAILED" if FAILS else "PASSED", len(FAILS), time.time() - t0))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
