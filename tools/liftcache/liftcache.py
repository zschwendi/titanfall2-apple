#!/usr/bin/env python3
"""The shared cache of lifted, compiled game modules (docs/handoff/gate3-build.md, "The cache").

Lifting the 20 boot-path DLLs and compiling the C takes minutes and gigabytes.  Every worktree
needs the result and almost none changes what it is made from, so it is built once, outside
the worktrees, and found again by a key over everything it depends on.

    liftcache.py ensure --manifest gate3/modules.def --out build/gate3/lifted [--jobs 6]
                        [--cflags "<flags>"] [--cc "xcrun clang"] [--no-patches]
        For every `lifted` row of the manifest: use the cache entry of its key, or build it.
        Writes <out>/<module>.objs and <out>/objects.filelist (absolute paths of the objects,
        for the linker; rewritten only when they change, so make sees a change exactly when
        there is one) and the links <out>/<module> to the entry's lifted C.
    liftcache.py key    --manifest ... [--cflags ...] [--cc ...]     key and hit/miss per module
    liftcache.py stats  --manifest ... [--cflags ...] [--cc ...]     what building each entry cost
    liftcache.py prune  [--keep 2]                                   drop all but the newest entries

The cache is <main checkout>/.cache/lifted (the checkout that owns the worktree; found through
git), or $LIFT_CACHE, or --cache.

    <cache>/<module>/<first 16 hex digits of the key>/
        KEY.json     the key and what went into it
        src/         what the lifter wrote: <module>_NNNN.c, <module>.h, the report
        obj/         one object per translation unit
        objects.txt  the objects, in link order
        tu.json      translation unit -> hash of everything its object depends on
        stats.json   what the build cost (time, memory, sizes)
        USED         touched by every `ensure` that uses the entry: prune goes by its time

The key of a module covers the DLL's bytes, the lifter's sources (the list lifter.lift.lifter_stamp
uses, which includes the table of guest patches, lifter/guest_patches.toml), whether the module
is lifted with those patches or without (--no-patches: entries of their own), the Python and
capstone versions, the headers lifted C includes (harness/*.h, harness/insns/*.h), the compiler
flags and the compiler's version.  Inside a miss, two things
are reused from the module's other entries by hard link, never rebuilt:
  - the lifted C, when an entry was lifted from the same DLL by the same lifter (the flags or a
    header changed, not the lifter);
  - the object of a translation unit whose C text, module header, flags, headers and compiler
    are the same (the lifter changed, but not what it writes for this file).

Two builders never write the same entry: a module is built under an exclusive lock on
<cache>/<module>/.lock, into a temporary directory that is renamed into place when complete.
A reader sees a whole entry or none.  Entries are made read-only.

Game files: this script hashes each DLL and runs the lifter on it; it reads nothing else of
the game and prints only names, counts and times.
"""

import argparse
import concurrent.futures
import fcntl
import glob
import hashlib
import json
import os
import shlex
import shutil
import stat
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FORMAT = "liftcache-1"          # change it to make every existing entry a miss
DEFAULT_CFLAGS = ("-O2 -g0 -fno-strict-aliasing -fwrapv "
                  "-mllvm -capture-tracking-max-uses-to-explore=1000000 -mllvm -dse-memoryssa-scanlimit=1500 "
                  "-mllvm -dse-memoryssa-walklimit=1500 -mllvm -dse-memoryssa-path-check-limit=300 "
                  "-mllvm -memssa-check-limit=1500 -mllvm -memdep-block-scan-limit=2000 "
                  "-mllvm -memdep-block-number-limit=2000 -Iharness")     # runtime/rt.mk says why
# The table of guest patches (lifter/guest_patches.py): what it lists changes the lifted C, so
# it is part of every key, like the lifter's sources.
GUEST_PATCH_TABLE = os.path.join(REPO, "lifter", "guest_patches.toml")

_print_lock = threading.Lock()


def say(text):
    with _print_lock:
        print(text, flush=True)


def sha_file(path, h=None):
    h = h or hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h


def sha_tree(paths):
    """One digest over the names (relative to the repository) and contents of `paths`."""
    h = hashlib.sha256()
    for p in sorted(paths):
        h.update(os.path.relpath(p, REPO).replace(os.sep, "/").encode() + b"\0")
        with open(p, "rb") as f:
            h.update(f.read())
        h.update(b"\0")
    return h.hexdigest()


def digest(*parts):
    h = hashlib.sha256()
    for p in parts:
        h.update(p.encode() + b"\0")
    return h.hexdigest()


def cache_root(arg):
    if arg:
        return os.path.abspath(arg)
    env = os.environ.get("LIFT_CACHE")
    if env:
        return os.path.abspath(env)
    try:
        common = subprocess.run(["git", "-C", REPO, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()
        main = os.path.dirname(common) if os.path.basename(common) == ".git" else REPO
    except (OSError, subprocess.CalledProcessError):
        main = REPO
    return os.path.join(main, ".cache", "lifted")


def read_manifest(path):
    """The `lifted` rows of a module list in the format of runtime/modules.def:
    [(lifter module name, DLL path)] in file order."""
    out = []
    for raw in open(path):
        f = raw.split("#", 1)[0].split()
        if len(f) < 2 or f[0] != "lifted":
            continue
        kv = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        name = f[1][:-4] if f[1].lower().endswith(".dll") else f[1]
        module = kv.get("module") or "".join(c if c.isalnum() else "_" for c in name.lower())
        if "dll" not in kv:
            sys.exit("liftcache: %s: lifted row %s has no dll=" % (path, f[1]))
        out.append((module, os.path.join(REPO, kv["dll"])))
    return out


class Inputs:
    """What every key of this worktree shares: the lifter, the headers, the flags, the compiler."""

    def __init__(self, cflags, cc, patches=True):
        self.cflags = " ".join(cflags.split())
        self.cc = shlex.split(cc)
        self.patches = patches                      # False: lift with --no-patches
        lifter_src = (glob.glob(os.path.join(REPO, "lifter", "*.py")) + glob.glob(os.path.join(REPO, "lifter", "insns", "*.py"))
                      + [os.path.join(REPO, "tools", "inventory", "pe_inventory.py"), GUEST_PATCH_TABLE])
        self.lifter = sha_tree(lifter_src)
        self.headers = sha_tree(glob.glob(os.path.join(REPO, "harness", "*.h")) + glob.glob(os.path.join(REPO, "harness", "insns", "*.h")))
        try:
            from importlib.metadata import version
            capstone = version("capstone")
        except Exception:                           # noqa: BLE001  (no capstone: the lifter will say so itself)
            capstone = "?"
        self.python = "python %d.%d capstone %s" % (sys.version_info[0], sys.version_info[1], capstone)
        # $LIFT_CACHE_SALT counts as part of the lifter: the self-test uses it to stand for a
        # change of the lifter that leaves the lifted C as it was; by hand it forces a new lift.
        if os.environ.get("LIFT_CACHE_SALT"):
            self.lifter = digest(self.lifter, os.environ["LIFT_CACHE_SALT"])
        try:
            v = subprocess.run(self.cc + ["--version"], capture_output=True, text=True, check=True).stdout
            self.cc_version = " | ".join(v.splitlines()[:2])
        except (OSError, subprocess.CalledProcessError) as e:
            sys.exit("liftcache: cannot run %s --version: %s" % (" ".join(self.cc), e))
        # what the object of a translation unit depends on besides its own text and <module>.h
        self.tu_salt = digest(FORMAT, self.headers, self.cflags, self.cc_version)

    def keys(self, module, dll):
        dll_sha = sha_file(dll).hexdigest()
        lift_key = digest(FORMAT, module, dll_sha, self.lifter, self.python, *([] if self.patches else ["no guest patches"]))
        key = digest(lift_key, self.tu_salt)
        return {"format": FORMAT, "module": module, "key": key, "lift_key": lift_key, "dll_sha256": dll_sha,
                "guest_patches": "as lifter/guest_patches.toml lists them" if self.patches else "none (--no-patches)",
                "lifter": self.lifter, "python": self.python, "headers": self.headers, "cflags": self.cflags,
                "cc": self.cc_version}


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def entries(mod_dir):
    """The complete entries of a module: [(directory, KEY.json)]."""
    out = []
    for d in sorted(glob.glob(os.path.join(mod_dir, "[0-9a-f]" * 16))):
        k = read_json(os.path.join(d, "KEY.json"))
        if k and k.get("format") == FORMAT:
            out.append((d, k))
    return out


def find_hit(mod_dir, keys):
    d = os.path.join(mod_dir, keys["key"][:16])
    k = read_json(os.path.join(d, "KEY.json"))
    return d if k and k.get("key") == keys["key"] else None


def touch_used(entry):
    """Record that the entry is in use.  The entry is read-only; USED is the one file that is not."""
    try:
        os.utime(os.path.join(entry, "USED"), None)
    except OSError:
        pass


def make_writable(path):
    for root, dirs, files in os.walk(path):
        os.chmod(root, 0o755)
    os.chmod(path, 0o755)


def remove_tree(path):
    if os.path.isdir(path) and not os.path.islink(path):
        make_writable(path)
        shutil.rmtree(path, ignore_errors=True)


def make_read_only(path):
    """Every file but USED, and the directories of the lifted C and of the objects: a lifter or
    a compiler pointed at an entry by mistake can neither change nor replace a file."""
    for root, dirs, files in os.walk(path):
        for f in files:
            if f != "USED":
                p = os.path.join(root, f)
                os.chmod(p, stat.S_IMODE(os.stat(p).st_mode) & ~0o222)
    for d in ("src", "obj"):
        os.chmod(os.path.join(path, d), 0o555)


def run(cmd, cwd, log, slots):
    """Run one process in a job slot, at low priority.  -> (exit status, wall s, cpu s, peak bytes)."""
    with slots:
        t0 = time.time()
        with open(log, "ab") as out:
            p = subprocess.Popen(["nice", "-n", "10"] + cmd, cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
            _, status, ru = os.wait4(p.pid, 0)
            p.returncode = os.waitstatus_to_exitcode(status)
        return p.returncode, time.time() - t0, ru.ru_utime + ru.ru_stime, ru.ru_maxrss


def tail(path, n=20):
    try:
        with open(path, errors="replace") as f:
            return "".join(f.readlines()[-n:])
    except OSError:
        return ""


class BuildError(Exception):
    pass


def build(module, dll, keys, mod_dir, inputs, slots, jobs):
    """Build the entry of `keys` under the module's lock.  -> (entry directory, stats).
    The caller holds the lock, so every temporary directory found here is a dead builder's."""
    for old in glob.glob(os.path.join(mod_dir, "tmp.*")):
        remove_tree(old)
    final = os.path.join(mod_dir, keys["key"][:16])
    if os.path.exists(final):                       # an entry of an older format, or a broken one
        remove_tree(final)
    tmp = os.path.join(mod_dir, "tmp.%s.%d" % (keys["key"][:16], os.getpid()))
    src, obj, logs = os.path.join(tmp, "src"), os.path.join(tmp, "obj"), os.path.join(tmp, "log")
    for d in (src, obj, logs):
        os.makedirs(d)
    siblings = entries(mod_dir)
    st = {"module": module, "built": time.strftime("%Y-%m-%dT%H:%M:%S"), "jobs": jobs, "cflags": inputs.cflags}
    t_start = time.time()
    try:
        # ---- the lifted C: from an entry with the same lift key, or from the lifter ----
        same = next((d for d, k in siblings if k.get("lift_key") == keys["lift_key"]), None)
        if same:
            for f in os.listdir(os.path.join(same, "src")):
                os.link(os.path.join(same, "src", f), os.path.join(src, f))
            st.update(lift_reused=True, lift_s=0.0, lift_cpu_s=0.0, lift_peak_bytes=0)
        else:
            code, wall, cpu, peak = run([sys.executable, "-m", "lifter.lift", "--module", module, "--dll", dll, "--out", src]
                                        + ([] if inputs.patches else ["--no-patches"]),
                                        REPO, os.path.join(logs, "lift.log"), slots)
            if code != 0:
                raise BuildError("the lifter failed on %s (status %d):\n%s" % (module, code, tail(os.path.join(logs, "lift.log"))))
            st.update(lift_reused=False, lift_s=round(wall, 1), lift_cpu_s=round(cpu, 1), lift_peak_bytes=peak)
        report = read_json(os.path.join(src, module + ".report.json")) or {}
        s = report.get("summary", {})
        st.update(functions=s.get("functions"), lifted=s.get("lifted"), skipped=s.get("skipped"),
                  instructions=s.get("instructions_total"))

        # ---- the objects: from an entry that compiled the same text the same way, or the compiler ----
        tus = sorted(glob.glob(os.path.join(src, module + "_*.c")))
        if not tus:
            raise BuildError("the lifter wrote no %s_*.c" % module)
        header = sha_file(os.path.join(src, module + ".h")).hexdigest()
        have = {}                                   # translation unit hash -> an object that exists
        for d, _ in siblings:
            for name, h in (read_json(os.path.join(d, "tu.json")) or {}).items():
                o = os.path.join(d, "obj", name + ".o")
                if os.path.exists(o):
                    have.setdefault(h, o)
        tu_hash, todo, reused = {}, [], 0
        for c in tus:
            name = os.path.basename(c)[:-2]
            h = sha_file(c, hashlib.sha256((inputs.tu_salt + header).encode())).hexdigest()
            tu_hash[name] = h
            if h in have:
                os.link(have[h], os.path.join(obj, name + ".o"))
                reused += 1
            else:
                todo.append(name)
        cmd_head = inputs.cc + inputs.cflags.replace("-Iharness", "-I" + os.path.join(REPO, "harness")).split() + ["-I."]
        times = {}

        def compile_one(name):
            r = run(cmd_head + ["-c", name + ".c", "-o", os.path.join(obj, name + ".o")], src, os.path.join(logs, name + ".log"), slots)
            times[name] = r
            return name, r[0]

        t_cc = time.time()
        pool = concurrent.futures.ThreadPoolExecutor(max_workers=jobs)
        try:
            for name, code in pool.map(compile_one, todo):
                if code != 0:
                    raise BuildError("compiling %s.c failed (status %d):\n%s" % (name, code, tail(os.path.join(logs, name + ".log"))))
        finally:
            pool.shutdown(wait=True, cancel_futures=True)   # after a failure: do not start the rest
        worst = max(times, key=lambda n: times[n][3]) if times else None
        slowest = max(times, key=lambda n: times[n][1]) if times else None
        st.update(tus=len(tus), compiled=len(todo), reused=reused,
                  compile_wall_s=round(time.time() - t_cc, 1) if todo else 0.0,
                  compile_cpu_s=round(sum(t[2] for t in times.values()), 1),
                  compile_peak_bytes=times[worst][3] if worst else 0, compile_peak_tu=worst,
                  compile_slowest_s=round(times[slowest][1], 1) if slowest else 0.0, compile_slowest_tu=slowest,
                  c_bytes=sum(os.path.getsize(c) for c in tus),
                  c_largest_bytes=max(os.path.getsize(c) for c in tus),
                  object_bytes=sum(os.path.getsize(os.path.join(obj, n + ".o")) for n in tu_hash),
                  total_wall_s=round(time.time() - t_start, 1))
        for f in os.listdir(logs):                  # keep the logs that say something
            if os.path.getsize(os.path.join(logs, f)) == 0:
                os.remove(os.path.join(logs, f))
        with open(os.path.join(tmp, "objects.txt"), "w") as f:
            f.write("".join("obj/%s.o\n" % n for n in sorted(tu_hash)))
        for name, data in (("tu.json", tu_hash), ("stats.json", st), ("KEY.json", keys)):
            with open(os.path.join(tmp, name), "w") as f:
                json.dump(data, f, indent=1, sort_keys=True)
        open(os.path.join(tmp, "USED"), "w").close()
        make_read_only(tmp)
        os.rename(tmp, final)
    except BaseException:
        remove_tree(tmp)
        raise
    return final, st


def ensure_one(module, dll, inputs, root, slots, jobs):
    keys = inputs.keys(module, dll)
    mod_dir = os.path.join(root, module)
    os.makedirs(mod_dir, exist_ok=True)
    hit = find_hit(mod_dir, keys)
    if hit:
        touch_used(hit)
        return module, hit, keys, None
    with open(os.path.join(mod_dir, ".lock"), "w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            say("  LIFTCACHE %-20s another build holds this module; waiting" % module)
            fcntl.flock(lock, fcntl.LOCK_EX)
        hit = find_hit(mod_dir, keys)               # the builder we waited for may have made it
        if hit:
            touch_used(hit)
            return module, hit, keys, None
        say("  LIFTCACHE %-20s miss %s: building" % (module, keys["key"][:16]))
        entry, st = build(module, dll, keys, mod_dir, inputs, slots, jobs)
        return module, entry, keys, st


def write_if_changed(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return False
    except OSError:
        pass
    with open(path + ".tmp", "w") as f:
        f.write(text)
    os.replace(path + ".tmp", path)
    return True


def mb(n):
    return "%.0f MB" % (n / 1e6) if n is not None else "?"


def cmd_ensure(args):
    inputs = Inputs(args.cflags, args.cc, not args.no_patches)
    root = cache_root(args.cache)
    mods = read_manifest(args.manifest)
    missing = [dll for _, dll in mods if not os.path.exists(dll)]
    if missing:
        sys.exit("liftcache: no %s" % missing[0])
    os.makedirs(args.out, exist_ok=True)
    slots = threading.BoundedSemaphore(args.jobs)
    t0 = time.time()
    results, failed = {}, []
    # The largest DLLs first: their lifts are the longest single steps.
    order = sorted(mods, key=lambda m: -os.path.getsize(m[1]))
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(mods)) as pool:
        futs = {pool.submit(ensure_one, m, dll, inputs, root, slots, args.jobs): m for m, dll in order}
        for fut in concurrent.futures.as_completed(futs):
            m = futs[fut]
            try:
                _, entry, keys, st = fut.result()
            except BuildError as e:
                failed.append(m)
                say("  LIFTCACHE %-20s FAILED: %s" % (m, e))
                continue
            results[m] = (entry, keys, st)
            if st:
                say("  LIFTCACHE %-20s built %s: lift %s, %d files (%d compiled in %.0f s, %d reused), objects %s, %.0f s"
                    % (m, keys["key"][:16], "reused" if st["lift_reused"] else "%.0f s" % st["lift_s"], st["tus"],
                       st["compiled"], st["compile_wall_s"], st["reused"], mb(st["object_bytes"]), st["total_wall_s"]))
    if failed:
        sys.exit("liftcache: %d of %d modules failed: %s" % (len(failed), len(mods), " ".join(failed)))
    all_objs = []
    for m, _ in mods:
        entry = results[m][0]
        with open(os.path.join(entry, "objects.txt")) as f:
            objs = [os.path.join(entry, line.strip()) for line in f if line.strip()]
        write_if_changed(os.path.join(args.out, m + ".objs"), "".join(o + "\n" for o in objs))
        all_objs += objs
        link = os.path.join(args.out, m)
        want = os.path.join(entry, "src")
        if not (os.path.islink(link) and os.readlink(link) == want):
            if os.path.lexists(link):
                os.remove(link)
            os.symlink(want, link)
    changed = write_if_changed(os.path.join(args.out, "objects.filelist"), "".join(o + "\n" for o in all_objs))
    write_if_changed(os.path.join(args.out, "keys.txt"),
                     "".join("%s %s %s\n" % (m, results[m][1]["key"], results[m][0]) for m, _ in mods))
    built = sum(1 for r in results.values() if r[2])
    say("  LIFTCACHE %d modules: %d from the cache, %d built, %d objects, %.1f s%s  (%s)"
        % (len(mods), len(mods) - built, built, len(all_objs), time.time() - t0, "" if changed else "; nothing changed", root))


def cmd_key(args):
    inputs = Inputs(args.cflags, args.cc, not args.no_patches)
    root = cache_root(args.cache)
    print("cache %s" % root)
    for m, dll in read_manifest(args.manifest):
        if not os.path.exists(dll):
            print("%-20s no DLL" % m)
            continue
        keys = inputs.keys(m, dll)
        print("%-20s %s %s" % (m, keys["key"][:16], "hit" if find_hit(os.path.join(root, m), keys) else "miss"))


def cmd_stats(args):
    """What building each current entry cost, as a table.  The numbers are those of the build
    that made the entry, whoever ran it."""
    inputs = Inputs(args.cflags, args.cc, not args.no_patches)
    root = cache_root(args.cache)
    cols = ["module", "functions", "skipped", "lift s", "lift peak", "files", "C", "largest file", "compiled", "compile CPU s",
            "slowest file s", "compiler peak", "objects"]
    rows, tot = [], {}
    for m, dll in read_manifest(args.manifest):
        keys = inputs.keys(m, dll) if os.path.exists(dll) else None
        entry = find_hit(os.path.join(root, m), keys) if keys else None
        st = read_json(os.path.join(entry, "stats.json")) if entry else None
        if not st:
            rows.append([m] + ["-"] * (len(cols) - 1))
            continue
        rows.append([m, st["functions"], st["skipped"], "reused" if st["lift_reused"] else st["lift_s"], mb(st["lift_peak_bytes"]),
                     st["tus"], mb(st["c_bytes"]), "%.1f MB" % (st["c_largest_bytes"] / 1e6),
                     "%d of %d" % (st["compiled"], st["tus"]), st["compile_cpu_s"], st["compile_slowest_s"],
                     mb(st["compile_peak_bytes"]), mb(st["object_bytes"])])
        for k in ("functions", "skipped", "lift_s", "tus", "c_bytes", "compiled", "compile_cpu_s", "object_bytes"):
            tot[k] = tot.get(k, 0) + (st.get(k) or 0)
        for k in ("lift_peak_bytes", "c_largest_bytes", "compile_slowest_s", "compile_peak_bytes"):
            tot[k] = max(tot.get(k, 0), st.get(k) or 0)
    if tot:
        rows.append(["all", tot["functions"], tot["skipped"], round(tot["lift_s"], 1), mb(tot["lift_peak_bytes"]) + " (max)", tot["tus"],
                     mb(tot["c_bytes"]), "%.1f MB" % (tot["c_largest_bytes"] / 1e6), "%d of %d" % (tot["compiled"], tot["tus"]),
                     round(tot["compile_cpu_s"], 1), "%s (max)" % tot["compile_slowest_s"], mb(tot["compile_peak_bytes"]) + " (max)",
                     mb(tot["object_bytes"])])
    print("| " + " | ".join(cols) + " |")
    print("|" + "---|" * len(cols))
    for r in rows:
        print("| " + " | ".join(str(x) for x in r) + " |")


def cmd_prune(args):
    """Keep, for each module, the `keep` entries used most recently; remove the others and
    whatever a dead builder left.  Takes each module's lock, so it waits for a build."""
    root = cache_root(args.cache)
    freed = removed = 0
    for mod_dir in sorted(glob.glob(os.path.join(root, "*"))):
        if not os.path.isdir(mod_dir):
            continue
        with open(os.path.join(mod_dir, ".lock"), "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            def used(d):
                try:
                    return os.stat(os.path.join(d, "USED")).st_mtime
                except OSError:
                    return 0
            dirs = sorted(glob.glob(os.path.join(mod_dir, "[0-9a-f]" * 16)), key=used, reverse=True)
            for d in dirs[args.keep:] + glob.glob(os.path.join(mod_dir, "tmp.*")):
                # Hard links are shared with the entries that stay: count what really goes.
                size = sum(os.lstat(os.path.join(r, f)).st_size for r, _, fs in os.walk(d) for f in fs
                           if os.lstat(os.path.join(r, f)).st_nlink == 1)
                remove_tree(d)
                freed += size
                removed += 1
                print("pruned %s" % d)
    print("liftcache prune: %d entries removed, about %s freed, %d kept per module  (%s)" % (removed, mb(freed), args.keep, root))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("ensure", cmd_ensure), ("key", cmd_key), ("stats", cmd_stats), ("prune", cmd_prune)):
        p = sub.add_parser(name)
        p.set_defaults(fn=fn)
        p.add_argument("--cache", help="cache directory (default: $LIFT_CACHE, or .cache/lifted of the main checkout)")
        if name == "prune":
            p.add_argument("--keep", type=int, default=2, help="entries kept per module (default 2)")
            continue
        p.add_argument("--manifest", required=True, help="a module list in the format of runtime/modules.def")
        p.add_argument("--cflags", default=DEFAULT_CFLAGS, help="flags for the lifted C (RT_LIFT_CFLAGS)")
        p.add_argument("--cc", default="xcrun clang", help="the compiler (RT_CC)")
        p.add_argument("--no-patches", action="store_true",
                       help="the modules lifted without the guest patches (lifter/guest_patches.py): entries of their own")
        if name == "ensure":
            p.add_argument("--out", required=True, help="where the object lists and the links to the lifted C go")
            p.add_argument("--jobs", type=int, default=6, help="processes at once, lifter and compiler together (default 6)")
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
