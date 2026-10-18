#!/usr/bin/env python3
"""make family-check FAMILY=<name>: the definition of done for one instruction family.

A family is three files: lifter/insns/<name>.py, harness/insns/<name>.h and
tests/vectors/<name>.py (docs/DESIGN.md, "Adding an instruction family").  This command
decides whether the family is done.  Exit status 0 = done, 1 = not done, 2 = could not run.

    make family-check FAMILY=sse_mov
    make family-check FAMILY=sse_mov BASE=some-branch FRESH=1

What is checked, in this order:

  1 files       Relative to the reference commit, nothing changed but the family's three files.
  - family      The lifter imports with the family in place, and the family registers something.
  - names       What the family's C header defines at file level is named after the family or
                one of its mnemonics (x_<name>..., TF_<NAME>...), so that two families written
                at the same time cannot define the same thing.  Applies to names the reference
                commit's copy of the header does not have.
  4 vectors     Every mnemonic the family registers has a hand-written vector in its file.
  2 selftests   'make test' passes.
  3 instances   The instruction test on real instances of the family's mnemonics, sampled per
                operand shape from every game and corpus DLL in the instruction index: no
                failure, every mnemonic that occurs has a passing case, and not too many
                shapes that never give a conclusive case.
  5 regression  tier0 and one corpus DLL are lifted and function-tested: no failure, and no
                function that lifted with the reference commit fails to lift now.

and it ends with one line a script can parse:

    FAMILY-CHECK family=<name> result=PASS|FAIL mnemonics=N game_instances=N cases_passed=N
                 cases_failed=N cases_inconclusive=N seconds=N

The reference commit is the merge base of HEAD and BASE (default wip/lifter-v0), the branch
the worktree was cut from.  (If that merge base is older than this check itself, or older than
a later generation of the shared harness that HEAD has - see GENERATIONS - the branch is the
one that introduces it, and the reference is HEAD.)

For a mnemonic its family declares float (fp=..., lifter/insns/README.md) item 3 also requires
that every tested instance ran with float inputs under every MXCSR variant and that no case
was left without a hardware answer, and it says how many cases the real CPU judged.  For a
mnemonic with a 16-byte memory operand that x86 requires to be aligned it requires a passing
case with the operand aligned and one with it misaligned (the #GP), and prints the counts.

It cannot be passed by editing the check.  This file only prepares a staging tree and hands
over to the reference commit's copy of itself: the staging tree is the reference commit plus
the family's three files from the worktree, and every check runs in it.  So the lifter core,
the harness, the self-tests, tools/ and the Makefile that judge the family are the reference
commit's, whatever the worktree holds; and a change outside the three files fails item 1.
(Someone who distrusts this file itself runs the reference copy directly:
'git show <base>:tools/family_check.py > /tmp/fc.py; python /tmp/fc.py --family <name>
--tree-of .'.)

Results are cached under harness/out/family-check/_cache/, keyed by the code of the reference
commit (lifter/, harness/, tests/, tools/, Makefile), the contents of the family files that
differ from it, the DLL and the sample; only passes are cached.  FRESH=1 ignores the cache.
"""

import argparse
import hashlib
import importlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))          # the tree this copy of the file is in
LINKS = (".venv", "game", "inventory", "third_party", "tests/corpus/build")      # shared, not in git

PER_SHAPE = 2       # instructions sampled per (operand shape, DLL); the index holds up to 8
# The candidate driver compares the whole image with its initial contents after every case, so a
# case costs time in proportion to SizeOfImage.  engine.dll maps to 350 MB (mostly zero-filled
# data) and would take most of the run: from a DLL whose image is larger than this, one
# instruction per shape is sampled instead of PER_SHAPE.
HUGE_IMAGE = 128 << 20
STATES = 24         # random states per instruction, as in 'make difftest-insn'
# A shape is inconclusive when none of its cases gives a verdict.  The check fails if more than
# this share of a mnemonic's tested shapes is inconclusive.  The eight families that existed
# when this was written have no such shape at all (docs/handoff/lifter-split.md); the limit
# leaves room for one in a mnemonic with ten or more shapes, for instance a form that a known
# oracle bug makes inconclusive.  Anything more is a shape nobody tested.
MAX_INCONCLUSIVE_SHARE = 0.10
# Generations of the shared harness, oldest first: a file whose presence in a commit says that
# commit can judge what the generation added (the family layout; float families; the emulator's
# compensations; the mnemonics only the real CPU judges, rcp and rsqrt; the vector values of the
# instruction test's states; the SSE compares with a predicate as translated instructions, which
# the self-tests used to demand refused, and the measurement of addsubps and haddps).  A branch
# whose merge base lacks one that its HEAD has is the branch that introduces that generation:
# it owns the shared files for that change, so its reference is HEAD.  A family worker's merge
# base has them all, and the rule never applies to it.
GENERATIONS = ("tools/family_check.py", "harness/floatcase.py", "harness/oraclefix.py",
               "harness/statefix.py", "harness/recip_sweep.py", "harness/vecstate.py", "harness/hadd_sweep.py")
REGRESSION_MODULE = "tier0"
REGRESSION_CORPUS = ("corpus_O2", "corpus_O1", "corpus_Os")     # the first that exists; else the first DLL there


def family_files(family):
    return ["lifter/insns/%s.py" % family, "harness/insns/%s.h" % family, "tests/vectors/%s.py" % family]


def git(tree, *args):
    return subprocess.run(["git", "-C", tree] + list(args), capture_output=True, text=True)


def sha(*parts):
    h = hashlib.sha256()
    for p in parts:
        h.update(p if isinstance(p, bytes) else str(p).encode())
        h.update(b"\0")
    return h.hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# ---- front: prepare the staging tree, hand over to the reference copy --------------------------------

def export(worktree, commit, dest):
    """The tracked files of `commit` in a new directory `dest`, plus the shared links."""
    os.makedirs(dest)
    arch = subprocess.Popen(["git", "-C", worktree, "archive", commit], stdout=subprocess.PIPE)
    tar = subprocess.run(["tar", "-x", "-C", dest], stdin=arch.stdout)
    arch.stdout.close()
    if arch.wait() != 0 or tar.returncode != 0:
        sys.exit("family-check: could not export commit %s" % commit)
    for rel in LINKS:
        src = os.path.join(worktree, rel)
        if os.path.exists(src):
            os.makedirs(os.path.dirname(os.path.join(dest, rel)), exist_ok=True)
            os.symlink(os.path.realpath(src), os.path.join(dest, rel))


def front(args):
    worktree = os.path.abspath(args.tree_of or ROOT)
    head = git(worktree, "rev-parse", "HEAD")
    if head.returncode != 0:
        print("family-check: %s is not a git worktree" % worktree)
        return 2
    head = head.stdout.strip()
    mb = git(worktree, "merge-base", "HEAD", args.base)
    if mb.returncode != 0:
        print("family-check: no merge base of HEAD and %r (set BASE to the branch this worktree was cut from)" % args.base)
        return 2
    mb = mb.stdout.strip()
    has = lambda commit, path: git(worktree, "cat-file", "-e", "%s:%s" % (commit, path)).returncode == 0
    new = [g for g in GENERATIONS if has(head, g) and not has(mb, g)]
    if not new:
        ref, why = mb, "the merge base of HEAD and %s" % args.base
    else:
        ref, why = head, ("HEAD: the merge base with %s (%s) is older than %s, so this branch is the one that "
                          "introduces it" % (args.base, mb[:10], new[0]))
    if git(worktree, "cat-file", "-e", "%s:tools/family_check.py" % ref).returncode != 0:
        print("family-check: commit %s has no tools/family_check.py (commit it first)" % ref[:10])
        return 2
    work = os.path.join(worktree, "harness", "out", "family-check", args.family)
    stage = os.path.join(work, "tree")
    shutil.rmtree(work, ignore_errors=True)
    export(worktree, ref, stage)
    for rel in family_files(args.family):                 # the only thing taken from the worktree
        src, dst = os.path.join(worktree, rel), os.path.join(stage, rel)
        if os.path.exists(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
        elif os.path.exists(dst):
            os.remove(dst)
    cmd = [sys.executable, os.path.join(stage, "tools", "family_check.py"), "--staged", "--family", args.family,
           "--worktree", worktree, "--ref", ref, "--ref-why", why, "--base", args.base]
    if args.fresh:
        cmd.append("--fresh")
    if args.keep:
        cmd.append("--keep")
    if args.jobs:
        cmd += ["--jobs", str(args.jobs)]
    return subprocess.call(cmd, cwd=stage)


# ---- one DLL of item 3 (a process of its own: the instruction test starts a process pool) -----------

def insn_worker(job_path):
    with open(job_path) as f:
        job = json.load(f)
    sys.path[:0] = [ROOT, os.path.join(ROOT, "harness")]
    import difftest
    from lifter import lift as L
    from lifter.discover import Func, Program
    from lifter.image import Image
    from lifter.translate import Translator
    image = Image.from_pe(job["dll"], job["module"])
    prog = Program(image)
    tr = Translator(image, image.name)
    refused, ok = {}, []
    for rva in job["rvas"]:
        ins = prog.decode(rva)
        if ins is None:
            refused[rva] = ["cannot decode"]
            continue
        # A LifterBug (the core's atomicity assertion) is not caught here: the worker dies and the check fails.
        rec, text, _, _ = L.lift_function(prog, tr, Func(rva, [(rva, rva + ins.size)], "insn"), "x", single=True)
        if text is None:
            refused[rva] = sorted({x["what"] for x in rec["reasons"]})
        else:
            ok.append(rva)
    result = {"module": job["module"], "refused": {"%d" % k: v for k, v in refused.items()}, "instructions": []}
    if ok:
        summary, _, insns = difftest.instruction_test(
            image, os.path.join(job["dir"], "lifted"), os.path.join(job["dir"], "run"), states=job["states"],
            jobs=job["jobs"], quiet=True, rvas=ok)
        result["instructions"] = insns
        result["not_lifted"] = summary["not_lifted"]
    with open(job["result"], "w") as f:
        json.dump(result, f)
    return 0


# ---- staged: the checks, run from the reference commit's copy of this file ---------------------------

class Report:
    def __init__(self):
        self.failed = []

    def item(self, tag, ok, text, details=()):
        print("[%s] %-4s %s" % (tag, "PASS" if ok else "FAIL", text))
        for d in details:
            print("                 " + d)
        sys.stdout.flush()
        if not ok:
            self.failed.append(tag.strip())


def code_id(worktree, ref):
    """Identifies the code of the reference commit that takes part in the checks: the tree ids of
    lifter/, harness/, tests/ and tools/ and the blob id of the Makefile.  A later commit that only
    touches documentation has the same code, and so finds the cached results."""
    r = git(worktree, "rev-parse", *["%s:%s" % (ref, p) for p in ("lifter", "harness", "tests", "tools", "Makefile")])
    return sha(r.stdout) if r.returncode == 0 else ref


def tool_versions():
    out = [sys.version]
    for name in ("capstone", "unicorn"):
        try:
            out.append("%s %s" % (name, importlib.import_module(name).__version__))
        except Exception as e:
            out.append("%s ? %r" % (name, e))
    cc = subprocess.run(["clang", "--version"], capture_output=True, text=True)
    out.append(cc.stdout.split("\n")[0] if cc.returncode == 0 else "no clang")
    return "\n".join(out)


class Cache:
    """Only passes are stored, so a cached entry can at worst repeat a pass for identical inputs."""

    def __init__(self, directory, fresh):
        self.dir, self.fresh = directory, fresh
        os.makedirs(directory, exist_ok=True)

    def get(self, kind, key):
        path = os.path.join(self.dir, "%s.%s.json" % (kind, key[:32]))
        if self.fresh or not os.path.exists(path):
            return None
        try:
            with open(path) as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def put(self, kind, key, value):
        path = os.path.join(self.dir, "%s.%s.json" % (kind, key[:32]))
        tmp = "%s.%d.tmp" % (path, os.getpid())
        with open(tmp, "w") as f:
            json.dump(value, f)
        os.replace(tmp, path)


def staged(args):
    t_start = time.time()
    fam, worktree, ref = args.family, args.worktree, args.ref
    files = family_files(fam)
    work = os.path.dirname(ROOT)                           # harness/out/family-check/<family>
    logs = os.path.join(work, "logs")
    os.makedirs(logs, exist_ok=True)
    cache = Cache(os.path.join(os.path.dirname(work), "_cache"), args.fresh)
    jobs = args.jobs or default_jobs()
    rep = Report()
    totals = {"mnemonics": 0, "game": 0, "pass": 0, "fail": 0, "inconclusive": 0}
    print("family-check %s: reference commit %s (%s)" % (fam, ref[:10], args.ref_why))

    def finish():
        result = "PASS" if not rep.failed else "FAIL"
        if not args.keep:                                  # the staging tree's build output is large
            for d in ("lifted", os.path.join("harness", "out")):
                shutil.rmtree(os.path.join(ROOT, d), ignore_errors=True)
        if rep.failed:
            print("not done: item%s %s failed; logs in %s" % ("s" if len(rep.failed) > 1 else "", ", ".join(rep.failed),
                                                              os.path.relpath(logs, worktree)))
        print("FAMILY-CHECK family=%s result=%s mnemonics=%d game_instances=%d cases_passed=%d cases_failed=%d "
              "cases_inconclusive=%d seconds=%d" % (fam, result, totals["mnemonics"], totals["game"], totals["pass"],
                                                    totals["fail"], totals["inconclusive"], time.time() - t_start))
        return 0 if result == "PASS" else 1

    # ---- 1: only the family's three files changed ----------------------------------------------------
    changed = set(git(worktree, "diff", "--name-only", "--no-renames", ref).stdout.split("\n"))
    changed |= set(git(worktree, "ls-files", "--others", "--exclude-standard").stdout.split("\n"))
    changed.discard("")
    outside = sorted(changed - set(files))
    mine = sorted(changed & set(files))
    if outside:
        rep.item("1 files     ", False, "%d path%s outside the family's three files changed relative to the reference:"
                 % (len(outside), "" if len(outside) == 1 else "s"),
                 outside[:12] + (["... and %d more" % (len(outside) - 12)] if len(outside) > 12 else []) + [
                     "These changes are NOT used below.  Every check runs the reference commit's copy of the",
                     "lifter core, the harness, the self-tests, tools/ and the Makefile; only %s" % ", ".join(files[:2]),
                     "and %s are taken from this worktree.  A family may not change anything else: if you" % files[2],
                     "need a change in a shared file, say so in your report (lifter/insns/README.md)."])
    else:
        rep.item("1 files     ", True, "changed relative to the reference: %s" % (", ".join(mine) or "nothing"))
    # What the staging tree is: the reference commit plus the family files that differ from it.  A
    # family that changed nothing is the reference tree itself, so its 'make test' and regression
    # results are shared with every other unchanged family.
    unchanged = not mine
    code = code_id(worktree, ref)
    tree_key = sha(code, tool_versions(), *[
        rel + ":" + (file_sha(os.path.join(ROOT, rel)) if os.path.exists(os.path.join(ROOT, rel)) else "absent")
        for rel in mine])

    # ---- the family itself ------------------------------------------------------------------------------
    if not os.path.exists(os.path.join(ROOT, files[0])):
        rep.item("- family    ", False, "there is no %s" % files[0])
        return finish()
    sys.path[:0] = [ROOT, os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools", "census"), os.path.join(ROOT, "harness")]
    try:
        from lifter import translate as T
    except Exception as e:
        rep.item("- family    ", False, "the lifter does not import with this family in place:",
                 ["%s: %s" % (type(e).__name__, e)])
        return finish()
    if fam not in T.FAMILIES or not T.FAMILIES[fam].mnemonics:
        rep.item("- family    ", False, "%s registers no mnemonic" % files[0])
        return finish()
    mnemonics = list(T.FAMILIES[fam].mnemonics)
    totals["mnemonics"] = len(mnemonics)
    rep.item("- family    ", True, "%d mnemonics registered; C header: %s" % (
        len(mnemonics), "harness/insns/" + T.FAMILIES[fam].header if T.FAMILIES[fam].header else "none"))

    # ---- names: every header is included in every generated C file, so they share one namespace ------
    check_names(rep, fam, files[1], mnemonics, worktree, ref)

    # ---- 4: a hand-written vector for every mnemonic ---------------------------------------------------
    check_vectors(rep, T, fam, files[2], mnemonics)

    # ---- 2: make test ------------------------------------------------------------------------------------
    t0 = time.time()
    if cache.get("selftests", tree_key):
        rep.item("2 selftests ", True, "make test passed (cached for this reference and these family files)")
    else:
        r = subprocess.run(["make", "-C", ROOT, "test"], capture_output=True, text=True)
        with open(os.path.join(logs, "make_test.log"), "w") as f:
            f.write(r.stdout + r.stderr)
        lines = [ln for ln in r.stdout.split("\n") if ln.strip()]
        bad = [ln.strip() for ln in lines if ln.startswith("  FAIL") or ln.startswith("  failed:")]
        if r.returncode == 0:
            cache.put("selftests", tree_key, {"ok": True})
        tail = [] if r.returncode == 0 else (bad[:10] or (r.stdout + r.stderr).strip().split("\n")[-8:])
        rep.item("2 selftests ", r.returncode == 0, "make test: %s" % (
            lines[-1].strip() if lines and r.returncode in (0, 1) else "exit status %d after %.0f s" % (
                r.returncode, time.time() - t0)), tail)

    # ---- 3: real instances ---------------------------------------------------------------------------------
    check_instances(rep, T, cache, tree_key, fam, mnemonics, totals, jobs, work, logs)

    # ---- 5: regression -------------------------------------------------------------------------------------
    check_regression(rep, cache, tree_key, worktree, ref, code, unchanged, jobs, work, logs)
    return finish()


C_NOT_NAMES = frozenset(["_Static_assert", "sizeof", "defined", "struct", "union", "enum", "typedef"])


def header_names(text):
    """The identifiers a C header defines at file level: macros, functions, variables, typedef
    names.  (Not what a macro pastes together when it is used, and not enumerators.)"""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text).replace("\\\n", " ")
    names, code = [], []
    for line in text.split("\n"):
        if line.lstrip().startswith("#"):
            m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)", line)
            if m:
                names.append(m.group(1))
        else:
            code.append(line)
    depth = paren = 0
    last = None
    for tok in re.findall(r"[A-Za-z_]\w*|[{}()\[\];=,]", "\n".join(code)):
        if tok == "{":
            depth, last = depth + 1, None
        elif tok == "}":
            depth, last = depth - 1, None
        elif depth or (paren and tok != ")" and tok != "("):
            continue
        elif tok == ")":
            paren, last = paren - 1, None
        elif tok in "([;=,":
            if not paren and last and not last.startswith("__") and last not in C_NOT_NAMES:
                names.append(last)
            paren += tok == "("
            last = None
        else:
            last = tok if tok[0].isalpha() or tok[0] == "_" else None
    return sorted(set(names))


def name_allowed(name, fam, mnemonics):
    """x_<key>, tf_<key> or <key> itself in either case, followed by nothing, digits, or _anything;
    <key> is the family's name or one of its mnemonics.  Or the include guard."""
    low = name.lower()
    if low == "tf_insns_%s_h" % fam:
        return True
    for key in [fam] + list(mnemonics):
        for pre in ("x_", "tf_"):
            if low.startswith(pre + key) and re.fullmatch(r"\d*(_\w*)?", low[len(pre) + len(key):]):
                return True
    return False


def check_names(rep, fam, path, mnemonics, worktree, ref):
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        rep.item("- names     ", True, "no C header")
        return
    old = git(worktree, "show", "%s:%s" % (ref, path))
    before = set(header_names(old.stdout)) if old.returncode == 0 else set()
    with open(full) as f:
        new = [n for n in header_names(f.read()) if n not in before]
    bad = [n for n in new if not name_allowed(n, fam, mnemonics)]
    rep.item("- names     ", not bad, "%d name%s defined by %s that the reference does not have%s" % (
        len(new), "" if len(new) == 1 else "s", path, "" if not bad else "; %d may clash with another family:" % len(bad)),
        [] if not bad else [" ".join(bad[:12]),
                            "Every header is included in every generated C file.  Name functions x_<mnemonic>,",
                            "x_<mnemonic>_<what> or x_%s_<what>, macros TF_%s_<WHAT>, and the include guard" % (fam, fam.upper()),
                            "TF_INSNS_%s_H." % fam.upper()])


def check_vectors(rep, T, fam, path, mnemonics):
    import capstone
    if not os.path.exists(os.path.join(ROOT, path)):
        rep.item("4 vectors   ", False, "there is no %s" % path)
        return
    try:
        import vectors
        vs = vectors.FAMILY_VECTORS[fam]
    except Exception as e:
        rep.item("4 vectors   ", False, "%s cannot be read: %s: %s" % (path, type(e).__name__, e))
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    have, foreign = Counter(), []
    for rva, hx, text in vs:
        ins = next(md.disasm(bytes.fromhex(hx), rva, 1), None)
        m = None if ins is None else T.mnemonic_of(ins)
        if T.FAMILY_OF.get(m) != fam:
            foreign.append("'%s' is %s, which this family does not register" % (text, m))
        else:
            have[m] += 1
    missing = [m for m in mnemonics if not have[m]]
    rep.item("4 vectors   ", not missing and not foreign, "%d hand-written vectors; %d of %d mnemonics have one" % (
        len(vs), len(mnemonics) - len(missing), len(mnemonics)),
        (["no vector for: " + " ".join(missing)] if missing else []) + foreign[:6])


def default_jobs():
    """Worker processes when --jobs is not given: all but two cores on a quiet machine, a third of
    them when the machine is already busy (1-minute load above the core count).  Several family
    checks run side by side in a wave; at nine processes each they pushed the load average past
    170 on eleven cores (2026-10-09) and the 10 s limit per case came within reach."""
    n = os.cpu_count() or 4
    try:
        busy = os.getloadavg()[0] > n
    except OSError:
        busy = False
    return max(2, n // 3) if busy else max(2, n - 2)


def check_instances(rep, T, cache, tree_key, fam, mnemonics, totals, jobs, work, logs):
    t0 = time.time()
    try:
        import insn_index
        index = insn_index.load(repo=ROOT, jobs=jobs, quiet=True)
    except Exception as e:
        rep.item("3 instances ", False, "the instruction index could not be loaded: %s: %s" % (type(e).__name__, e))
        return
    # The DLLs that are sampled: the game's and the corpus's.  Group "redist" (d3dcompiler_46.dll) is
    # indexed but left out: it is not game code, and the lifter's discovery takes pointer tables in
    # its code section for code, so "reached" means little there (docs/handoff/lifter-split.md).
    not_sampled = sorted(m for m, e in index.items() if e["group"] == "redist")
    index = {m: e for m, e in index.items() if e["group"] != "redist"}
    in_index = Counter()            # mnemonic -> instances in the sampled DLLs
    reached = Counter()
    for e in index.values():
        for m in mnemonics:
            rec = e["mnemonics"].get(m)
            if rec:
                in_index[m] += rec["count"]
                reached[m] += rec["reached"]
                if e["group"] == "game":
                    totals["game"] += rec["count"]
    # The sample: per DLL and operand shape, the first PER_SHAPE addresses the index holds.  A shape
    # that only occurs in bytes the lifter never reaches (data decoded by the sweep) is left out,
    # unless the mnemonic occurs nowhere else.
    plan, of, data_only_shapes = {}, {}, 0
    huge = []
    for mod in sorted(index):
        e = index[mod]
        rvas = []
        per_shape = PER_SHAPE
        if e["size"] > (1 << 20) and image_size(e["abs_path"]) > HUGE_IMAGE:
            per_shape = 1
            huge.append(mod)
        for m in mnemonics:
            for shp, s in sorted(e["mnemonics"].get(m, {}).get("shapes", {}).items()):
                if s["reached"] == 0 and reached[m] > 0:
                    data_only_shapes += 1
                    continue
                for rva in s["rvas"][:per_shape]:
                    rvas.append(rva)
                    of[(mod, rva)] = (m, shp)
        if rvas:
            plan[mod] = sorted(set(rvas))
    n_par = max(1, min(4, jobs // 2, len(plan)))
    per_job = max(2, jobs // n_par)
    insn_dir = os.path.join(work, "insn")
    os.makedirs(insn_dir, exist_ok=True)

    def run_module(mod):
        e = index[mod]
        key = sha(tree_key, fam, mod, e["sha256"], json.dumps(plan[mod]), STATES)
        hit = cache.get("insn", key)
        if hit is not None:
            return mod, hit, True, ""
        t1 = time.time()
        d = os.path.join(insn_dir, mod)
        os.makedirs(d, exist_ok=True)
        job = {"dll": e["abs_path"], "module": mod, "rvas": plan[mod], "states": STATES, "jobs": per_job, "dir": d,
               "result": os.path.join(d, "result.json")}
        with open(os.path.join(d, "job.json"), "w") as f:
            json.dump(job, f)
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--insn-worker", os.path.join(d, "job.json")],
                           cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(job["result"]):
            with open(os.path.join(logs, "insn_%s.log" % mod), "w") as f:
                f.write(r.stdout + r.stderr)
            return mod, None, False, (r.stderr.strip().split("\n") or ["?"])[-1]
        with open(job["result"]) as f:
            res = json.load(f)
        seconds[mod] = round(time.time() - t1, 1)
        if not any(i["fail"] for i in res["instructions"]):
            cache.put("insn", key, res)
        shutil.rmtree(d, ignore_errors=True)
        return mod, res, False, ""

    seconds = {}
    order = sorted(plan, key=lambda m: -index[m]["size"])
    with ThreadPoolExecutor(n_par) as ex:
        results = list(ex.map(run_module, order))

    per_mn = {m: Counter() for m in mnemonics}          # pass, fail, inconclusive, tested, refused
    per_shape = {}                                      # (mnemonic, shape) -> Counter
    why_inconclusive, why_refused = Counter(), Counter()
    failures, broken, n_cached = [], [], 0
    import floatcase as FC
    fl = {m: Counter() for m in mnemonics if m in T.FLOAT}      # the float mnemonics: where expected results came from
    fl_bad = []
    m128 = {m: Counter() for m in mnemonics}            # instances with a 16-byte operand that must be aligned
    for mod, res, was_cached, err in results:
        if res is None:
            broken.append("%s: the instruction test did not run (%s)" % (mod, err))
            continue
        n_cached += was_cached
        for rva, reasons in res["refused"].items():
            m, shp = of[(mod, int(rva))]
            per_mn[m]["refused"] += 1
            per_shape.setdefault((m, shp), Counter())["refused"] += 1
            why_refused["%s: %s" % (m, "; ".join(reasons))] += 1
        for i in res["instructions"]:
            m, shp = of[(mod, int(i["rva"], 16))]
            c = per_shape.setdefault((m, shp), Counter())
            for k, src in (("pass", "pass"), ("fail", "fail"), ("inconclusive", "inconclusive")):
                c[k] += i[src]
                per_mn[m][k] += i[src]
                totals[k] += i[src]
            c["tested"] += 1
            per_mn[m]["tested"] += 1
            if m in fl:
                f = i.get("float")
                if not f or set(f["variants"]) != set(FC.VARIANT_NAMES) or min(f["variants"].values()) < STATES:
                    fl_bad.append("%s %s  %s: did not run with float inputs under every MXCSR variant" % (mod, i["rva"], i["text"]))
                else:
                    fl[m].update({k: v for k, v in f.items() if isinstance(v, int)})
                    fl[m].update({"differs under " + k: v for k, v in f.get("emulator_differs_by_variant", {}).items()})
                    if f["no_hardware_answer"]:
                        fl_bad.append("%s %s  %s: %d cases under a non-default MXCSR have no hardware answer" % (
                            mod, i["rva"], i["text"], f["no_hardware_answer"]))
            if "m128_operand" in i:
                m128[m].update(i["m128_operand"], instances=1)
            if i["inconclusive"]:
                why_inconclusive[" + ".join(i.get("inconclusive_reasons", ["?"]))] += i["inconclusive"]
            if i["fail"]:
                failures.append("%s %s  %s: %s" % (mod, i["rva"], i["text"], i["first_failure"]["mismatches"][0]))
    with open(os.path.join(logs, "instances.json"), "w") as f:
        json.dump({"per_mnemonic": per_mn, "per_shape": {"%s | %s" % k: v for k, v in sorted(per_shape.items())},
                   "failures": failures, "refused": why_refused, "inconclusive": why_inconclusive, "float": fl,
                   "float_not_done": fl_bad, "m128_operand": {m: c for m, c in m128.items() if c},
                   "seconds_per_dll": seconds, "instructions_per_dll": {m: len(v) for m, v in plan.items()}}, f, indent=1)

    absent = [m for m in mnemonics if in_index[m] == 0]
    no_pass = [m for m in mnemonics if in_index[m] and per_mn[m]["pass"] == 0]
    too_inconclusive = []
    worst = (0.0, "")
    for m in mnemonics:
        shapes = [c for (mm, _), c in per_shape.items() if mm == m and c["tested"]]
        dead = sum(1 for c in shapes if c["pass"] + c["fail"] == 0)
        if shapes:
            share = dead / len(shapes)
            worst = max(worst, (share, "%s: %d of %d shapes" % (m, dead, len(shapes))))
            if share > MAX_INCONCLUSIVE_SHARE:
                too_inconclusive.append("%s: %d of its %d tested shapes never gave a conclusive case" % (m, dead, len(shapes)))
    details = []
    details += ["FAIL " + x for x in failures[:12]]
    if len(failures) > 12:
        details.append("... and %d more failing instructions (logs/instances.json)" % (len(failures) - 12))
    details += broken[:6]
    details += ["FLOAT " + x for x in fl_bad[:8]] + (["... and %d more (logs/instances.json)" % (len(fl_bad) - 8)]
                                                     if len(fl_bad) > 8 else [])
    for m in sorted(fl):
        f = fl[m]
        details.append("float %s: %d cases under %d MXCSR variants; expected side: the real CPU %d, emulator only %d, "
                       "no hardware answer %d; the emulator differs from the CPU in %d (%s); the NaN correction changed %d" % (
                           m, f["cases"], len(FC.VARIANTS), f["hardware"], f["emulator_only"], f["no_hardware_answer"],
                           f["emulator_differs_from_hardware"],
                           ", ".join("%s %d" % (v, f["differs under " + v]) for v in FC.VARIANT_NAMES if f["differs under " + v])
                           or "none", f["nan_correction_changed"]))
    # A 16-byte memory operand that must be aligned (lifter/insns/README.md, rule 3): the
    # mnemonic's instances together must have passed with it aligned (the result) and, where
    # the test could misalign it, misaligned (the #GP).
    m128_bad = []
    for m in sorted(m128):
        c = m128[m]
        if not c:
            continue
        details.append("m128 %s: %d instances with a 16-byte operand that must be aligned; aligned cases %d (%d pass), "
                       "misaligned cases, expecting #GP, %d (%d pass)" % (
                           m, c["instances"], c["aligned_cases"], c["aligned_pass"], c["misaligned_cases"], c["misaligned_pass"]))
        if c["aligned_pass"] == 0:
            m128_bad.append("%s: no case with an aligned 16-byte operand passed" % m)
        if c["misaligned_cases"] and c["misaligned_pass"] == 0:
            m128_bad.append("%s: no case with a misaligned 16-byte operand passed (the instruction must trap: tr.xval, tr.xstore)" % m)
    details += ["M128 " + x for x in m128_bad]
    if no_pass:
        details.append("in the index but without a passing case: " + ", ".join(
            "%s (%d instances; %d samples refused, %d inconclusive cases)" % (
                m, in_index[m], per_mn[m]["refused"], per_mn[m]["inconclusive"]) for m in no_pass))
    details += ["more than %d%% of shapes inconclusive - %s" % (100 * MAX_INCONCLUSIVE_SHARE, x) for x in too_inconclusive]
    if absent:
        details.append("absent from the index (no instance in any sampled DLL, so tested by vectors only): " + " ".join(absent))
    if why_inconclusive:
        details.append("inconclusive cases by reason: " + "; ".join("%s %d" % kv for kv in why_inconclusive.most_common(5)))
    if why_refused:
        details.append("samples the family refuses (stay 'unsupported', not counted): " + "; ".join(
            "%s x%d" % kv for kv in why_refused.most_common(5)) + (" ..." if len(why_refused) > 5 else ""))
    if not_sampled:
        details.append("indexed but not sampled (not game code): " + ", ".join(not_sampled))
    if huge:
        details.append("one instruction per shape instead of %d from %s (image larger than %d MB)" % (
            PER_SHAPE, ", ".join(huge), HUGE_IMAGE >> 20))
    if data_only_shapes:
        details.append("%d (shape, DLL) pairs left out: they occur only in bytes the lifter never reaches" % data_only_shapes)
    details.append("worst share of inconclusive shapes: %s (limit %d%%)" % (
        worst[1] or "none", 100 * MAX_INCONCLUSIVE_SHARE))
    tested = sum(c["tested"] for c in per_mn.values())
    ok = not failures and not broken and not no_pass and not too_inconclusive and not fl_bad and not m128_bad
    rep.item("3 instances ", ok, "%d instructions from %d DLLs (%d cached): cases pass %d, fail %d, inconclusive %d; "
             "%d of %d mnemonics have a passing case (%.0f s)" % (
                 tested, len(plan), n_cached, totals["pass"], totals["fail"], totals["inconclusive"],
                 sum(1 for m in mnemonics if per_mn[m]["pass"]), len(mnemonics), time.time() - t0), details)


def image_size(path):
    """SizeOfImage of a PE32+ file, read from its optional header (0 if it cannot be read)."""
    try:
        with open(path, "rb") as f:
            head = f.read(4096)
        e_lfanew = int.from_bytes(head[0x3C:0x40], "little")
        return int.from_bytes(head[e_lfanew + 24 + 56:e_lfanew + 24 + 60], "little")
    except (OSError, IndexError):
        return 0


def corpus_dll():
    d = os.path.join(ROOT, "tests", "corpus", "build")
    for name in REGRESSION_CORPUS:
        if os.path.exists(os.path.join(d, name + ".dll")):
            return name, os.path.join(d, name + ".dll")
    dlls = sorted(f for f in os.listdir(d) if f.endswith(".dll")) if os.path.isdir(d) else []
    return (dlls[0][:-4], os.path.join(d, dlls[0])) if dlls else (None, None)


def lift(tree, module, dll, log):
    """Lift `module` with the lifter in `tree`; returns the names of the functions that lifted, or None."""
    cmd = [sys.executable, "-m", "lifter.lift", "--module", module] + (["--dll", dll] if dll else [])
    r = subprocess.run(cmd, cwd=tree, capture_output=True, text=True)
    with open(log, "w") as f:
        f.write(r.stdout + r.stderr)
    report = os.path.join(tree, "lifted", module, module + ".report.json")
    if r.returncode != 0 or not os.path.exists(report):
        return None
    with open(report) as f:
        return sorted(x["name"] for x in json.load(f)["functions"] if x["status"].startswith("lifted"))


def check_regression(rep, cache, tree_key, worktree, ref, code, unchanged, jobs, work, logs):
    t0 = time.time()
    sys.path.insert(0, ROOT)
    from lifter.image import find_module
    cname, cpath = corpus_dll()
    targets = [(REGRESSION_MODULE, None, find_module(REGRESSION_MODULE))]
    if cname:
        targets.append((cname, cpath, cpath))
    ok, lines, details = True, [], []
    ref_tree = os.path.join(os.path.dirname(work), "_ref-" + ref[:12])
    for module, dll_arg, dll_path in targets:
        dsha = file_sha(dll_path)
        key = sha(tree_key, module, dsha)
        hit = cache.get("regression", key)
        if hit is not None:
            lines.append("%s: %d functions lifted, %d passing, 0 failing, %d inconclusive (cached)" % (
                module, hit["lifted"], hit["passing"], hit["inconclusive"]))
            continue
        now = lift(ROOT, module, dll_arg, os.path.join(logs, "lift_%s.log" % module))
        if now is None:
            ok = False
            details.append("%s: the lift failed (logs/lift_%s.log)" % (module, module))
            continue
        # What lifted with the reference commit.  Identical family files give an identical lifter.
        base = now if unchanged else cache.get("base", sha(code, module, dsha))
        if base is None:
            if not os.path.isdir(ref_tree):
                tmp = "%s.%d" % (ref_tree, os.getpid())
                export(worktree, ref, tmp)
                try:
                    os.rename(tmp, ref_tree)
                except OSError:
                    shutil.rmtree(tmp, ignore_errors=True)
            base = lift(ref_tree, module, dll_arg, os.path.join(logs, "lift_ref_%s.log" % module))
            shutil.rmtree(os.path.join(ref_tree, "lifted", module), ignore_errors=True)
            if base is None:
                ok = False
                details.append("%s: the reference commit's lifter failed (logs/lift_ref_%s.log)" % (module, module))
                continue
            cache.put("base", sha(code, module, dsha), base)
        lost = sorted(set(base) - set(now))
        gained = len(set(now) - set(base))
        r = subprocess.run([sys.executable, "harness/difftest.py", "func", "--module", module, "--jobs", str(jobs)]
                           + (["--dll", dll_arg] if dll_arg else []), cwd=ROOT, capture_output=True, text=True)
        with open(os.path.join(logs, "func_%s.log" % module), "w") as f:
            f.write(r.stdout + r.stderr)
        out = os.path.join(ROOT, "harness", "out", module, "difftest_func.json")
        if r.returncode not in (0, 1) or not os.path.exists(out):
            ok = False
            details.append("%s: the function test did not run (exit status %d, logs/func_%s.log)" % (
                module, r.returncode, module))
            continue
        with open(out) as f:
            res = json.load(f)
        s = res["summary"]
        lines.append("%s: %d functions lifted (%d more than the reference, %d fewer), %d passing, %d failing, "
                     "%d inconclusive" % (module, len(now), gained, len(lost), s["passing"], s["failing"], s["inconclusive"]))
        if lost:
            ok = False
            details.append("%s: lifted with the reference commit, not any more: %s%s" % (
                module, " ".join(lost[:8]), " ..." if len(lost) > 8 else ""))
        if s["failing"]:
            ok = False
            for fn in [x for x in res["functions"] if x["status"] == "failing"][:8]:
                details.append("FAIL %s: %s" % (fn["name"], fn["failures"][0]["mismatches"][0]))
        if not lost and not s["failing"]:
            cache.put("regression", key, {"lifted": len(now), "passing": s["passing"], "inconclusive": s["inconclusive"]})
    rep.item("5 regression", ok, "%s (%.0f s)" % ("; ".join(lines) or "nothing ran", time.time() - t0), details)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--family")
    ap.add_argument("--base", default="wip/lifter-v0", help="the branch this worktree was cut from")
    ap.add_argument("--fresh", action="store_true", help="ignore cached results")
    ap.add_argument("--keep", action="store_true", help="keep the staging tree's lifted C and test output")
    ap.add_argument("--jobs", type=int)
    ap.add_argument("--tree-of", help="the worktree to check (default: the one this file is in)")
    ap.add_argument("--staged", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--worktree", help=argparse.SUPPRESS)
    ap.add_argument("--ref", help=argparse.SUPPRESS)
    ap.add_argument("--ref-why", default="", help=argparse.SUPPRESS)
    ap.add_argument("--insn-worker", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.insn_worker:
        return insn_worker(args.insn_worker)
    if not args.family or not args.family.replace("_", "a").isalnum():
        print("usage: make family-check FAMILY=<name>     (a family is lifter/insns/<name>.py)")
        return 2
    if args.staged:
        return staged(args)
    return front(args)


if __name__ == "__main__":
    sys.exit(main())
