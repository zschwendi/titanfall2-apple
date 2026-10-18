#!/usr/bin/env python3
"""The "done" check of the two tasks of a core piece (tools/swarm/make_core_tasks.py has the table).

    core_check.py test <piece>     the task that types runtime/tests/core/pending/<test>.c
    core_check.py body <piece>     the task that writes the body and moves the test up

    --base-ref REF   the branch the worktree was cut from (default wip/lifter-v0); the changes
                     of the task are everything that differs from the merge base with it
    --base COMMIT    an explicit base instead
    --make MAKE      the make to call (default make)

Run from the root of the worktree (`make core-test-check PIECE=<piece>` does).  Exit 0: done.
Every item is printed as `[n] ok` or `[n] FAILED` with what is wrong; the items after a failed
build are not run.  Uncommitted and untracked files count as changes.

test:  [1] the task adds exactly runtime/tests/core/pending/<test>.c
       [2] its TESTs are named <test> or <test>_<word>, and every case number of the plan is
           exactly one T_PLAN(n, ...) block inside a TEST
       [3] it compiles with no warning as the suite compiles a test, in a temporary directory
           (as if it were in runtime/tests/core/), and links with the suite
       [4] each case, run alone against today's stub (RT_TEST_PLAN_CASE=n), starts and does
           not pass.  A case that passes because a T_TRAPS met the stub's own stop is reported
           and allowed; one that passes without reaching the stub is refused
       [5] make rt-test passes (the pending file is not part of it)
body:  [1] the task changes only the piece's sources, the test (pending/ -> tests/core/), the
           piece's unbuilt test (deleted) and what the table lists under `also`
       [2] runtime/tests/core/<test>.c is byte for byte the pending file of the base
       [3] no RT_CORE_UNIMPLEMENTED is left in the sources (unless the table says one stays)
       [4] make rt-test passes
       [5] every case of the plan ran in the piece's tests, made a check and failed none
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_core_tasks as table                                      # noqa: E402

STOP = "unimplemented runtime core function"
failed = []


def run(argv, env=None, timeout=3600):
    try:
        r = subprocess.run(argv, capture_output=True, text=True, errors="replace", env=env, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired as e:
        return "timeout", (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or ""), ""


def item(n, problems, notes=()):
    for x in notes:
        print("    note: " + x)
    if problems:
        failed.append(n)
        print("[%d] FAILED" % n)
        for x in problems[:30]:
            print("    " + x.replace("\n", "\n    "))
    else:
        print("[%d] ok" % n)
    sys.stdout.flush()
    return not problems


def tail(text, n=25):
    return "\n".join(text.rstrip("\n").split("\n")[-n:])


def base_commit(args):
    if args.base:
        ref = args.base
    else:
        rc, out, err = run(["git", "merge-base", "HEAD", args.base_ref])
        if rc:
            sys.exit("cannot find the base: git merge-base HEAD %s failed (%s); pass --base <commit> "
                     "(make: RT_BASE=<commit>)" % (args.base_ref, err.strip()))
        ref = out.strip()
    rc, out, err = run(["git", "rev-parse", "--verify", ref + "^{commit}"])
    if rc:
        sys.exit("the base %r is not a commit: %s" % (ref, err.strip()))
    return out.strip()


def changes(base):
    """{path: 'A' | 'M' | 'D'}: what differs from the base, committed or not, untracked included."""
    out = {}
    rc, text, err = run(["git", "diff", "--name-status", "--no-renames", base])
    if rc:
        sys.exit("git diff failed: " + err.strip())
    for line in text.splitlines():
        st, path = line.split("\t", 1)
        out[path] = st[0]
    rc, text, _ = run(["git", "ls-files", "--others", "--exclude-standard"])
    for path in text.splitlines():
        out[path] = "A"
    return out


def strip_comments(code):
    code = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), code, flags=re.S)
    return re.sub(r"//[^\n]*", "", code)


def layout(code, p):
    """([(test name, [case numbers])], problems) from the text of a test file."""
    code = strip_comments(code)
    problems, tests = [], []
    marks = [(m.start(), "T", m.group(1)) for m in re.finditer(r"^TEST\(\s*core\s*,\s*(\w+)\s*\)", code, re.M)]
    marks += [(m.start(), "P", int(m.group(1))) for m in re.finditer(r"\bT_PLAN\s*\(\s*(\d+)\s*,", code)]
    for m in re.finditer(r"\bT_PLAN\s*\((?!\s*\d+\s*,)", code):
        problems.append("line %d: T_PLAN's first argument must be the plan's case number as a decimal literal"
                        % (code.count("\n", 0, m.start()) + 1))
    for pos, kind, val in sorted(marks):
        if kind == "T":
            if not re.fullmatch(re.escape(p["test"]) + r"(_\w+)?", val):
                problems.append("TEST(core, %s): a test of this file is named %s or %s_<word>" % (val, p["test"], p["test"]))
            tests.append((val, []))
        elif not tests:
            problems.append("T_PLAN(%d, ...) stands before the first TEST" % val)
        else:
            tests[-1][1].append(val)
    if not tests:
        problems.append("the file has no TEST(core, %s) starting in column 1" % p["test"])
    seen = [n for _, ns in tests for n in ns]
    want = table.plan_cases(p)
    for n in want:
        if seen.count(n) != 1:
            problems.append("case %d of the plan: %s" % (n, "no T_PLAN(%d, ...) block" % n if n not in seen
                                                            else "%d T_PLAN blocks, expected one" % seen.count(n)))
    for n in sorted(set(seen) - set(want)):
        problems.append("T_PLAN(%d, ...): the plan has no case %d (its cases: %s)" % (n, n, ", ".join(map(str, want))))
    if '#include "rt/test.h"' not in code:
        problems.append('the file must contain #include "rt/test.h"')
    return tests, problems


def make(args, *words):
    return run([args.make, *words])


def suite(args, n):
    rc, out, err = make(args, "rt-test")
    if rc == 0:
        return item(n, [])
    # The failed tests and their failed checks (harness: "FAIL  lib/name" and "  file:line: ..."); a build error otherwise.
    lines = [l for l in out.splitlines() if l.startswith("FAIL")][:15]
    lines += [l for l in err.splitlines() if re.match(r"  \S+:\d+: ", l)][:25]
    return item(n, ["make rt-test failed (%s):\n%s" % (rc, "\n".join(lines) if lines else tail(out + err))])


def survey(binary, tests, p):
    """Run every case alone against the stub.  (problems, notes)."""
    problems, notes = [], []
    for name, cases in tests:
        for n in cases:
            env = dict(os.environ, RT_TEST_PLAN_CASE=str(n))
            rc, out, err = run([binary, "-v", "--report", "core/" + name], env=env, timeout=300)
            started = re.search(r"^    case: %d: " % n, err, re.M) is not None
            rep = re.search(r"^#rt case (\d+) (\d+) %d: " % n, out, re.M)
            stop = STOP in err and ("(unit of work: %s)" % p["tag"]) in err
            if rc == 2:
                problems.append("case %d: the test program refused to run core/%s: %s" % (n, name, tail(out + err, 3)))
            elif not started:
                problems.append("case %d never started: something before its block stops or hangs, or the block is "
                                "compiled out (TEST core/%s, exit %s):\n%s" % (n, name, rc, tail(err, 6)))
            elif rep and int(rep.group(2)) == 0:
                if int(rep.group(1)) == 0:
                    problems.append("case %d makes no check" % n)
                elif stop and "trapped as expected" in err:
                    notes.append("case %d passes against the stub because a T_TRAPS met the stub's stop "
                                 "(make its text the exact text of the plan if it is not)" % n)
                elif n in p.get("stub_pass", ()):
                    notes.append("case %d passes against the stub; the table accepts that for this case (stub_pass)" % n)
                else:
                    problems.append("case %d passes against the stub (%s checks, none failed) without reaching a "
                                    "function of the piece: it tests nothing" % (n, rep.group(1)))
            elif rep:
                notes.append("case %d: fails %s of %s checks against the stub" % (n, rep.group(2), rep.group(1)))
            elif rc == "timeout":
                notes.append("case %d: did not end in 300 s against the stub (it should not wait for ever when a "
                             "function of the piece stops)" % n)
            elif not stop:
                notes.append("case %d: the test ended (exit %s) without the stub's report: %s" % (n, rc, tail(err, 2)))
    notes.append("%d cases run alone against the stub; %d refused" % (sum(len(c) for _, c in tests), len(problems)))
    return problems, notes


def check_test(p, args):
    base = base_commit(args)
    pending = "runtime/tests/core/pending/%s.c" % p["test"]
    ch = changes(base)
    problems = ["%s is not added by this branch (it must be a new file)" % pending] if ch.get(pending) != "A" else []
    problems += ["%s: %s; the task adds %s and touches nothing else" % (
        f, {"A": "added", "M": "modified", "D": "deleted"}.get(s, s), pending) for f, s in sorted(ch.items()) if f != pending]
    item(1, problems)
    if not os.path.exists(pending):
        return
    tests, problems = layout(open(pending, errors="replace").read(), p)
    ok2 = item(2, problems)

    tmp = tempfile.mkdtemp(prefix="core-check-")
    try:
        shutil.copy(pending, os.path.join(tmp, p["test"] + ".c"))
        link = "1" if p["tag"] else "0"
        rc, out, err = make(args, "rt-test-pending", "PENDING=%s/%s.c" % (tmp, p["test"]), "PENDING_OUT=" + tmp,
                            "PENDING_LINK=" + link)
        notes = [] if p["tag"] else ["the piece has no stub to link with: the file was compiled, not run"]
        ok3 = item(3, [] if rc == 0 else ["the file does not build (%s):\n%s" % (rc, tail(out + err, 40))], notes)
        if ok3 and ok2 and p["tag"]:
            problems, notes = survey(os.path.join(tmp, "rt-test"), tests, p)
            item(4, problems, notes)
        elif p["tag"]:
            item(4, ["not run: items 2 and 3 must pass first"])
        else:
            item(4, [], ["not run (no stub)"])
        if ok3:
            suite(args, 5)
        else:
            item(5, ["not run: item 3 must pass first"])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def check_body(p, args):
    base = base_commit(args)
    pending = "runtime/tests/core/pending/%s.c" % p["test"]
    final = "runtime/tests/core/%s.c" % p["test"]
    ch = changes(base)
    allowed = set(p["sources"]) | {pending, final} | set(p.get("also", [])) | ({p["unbuilt"]} if p.get("unbuilt") else set())
    problems = ["%s: changed by this branch; the task may change only %s" % (f, ", ".join(sorted(allowed)))
                for f in sorted(ch) if f not in allowed]
    problems += ["%s: the body task writes it and it is unchanged" % s for s in p["sources"] if s not in ch]
    if p.get("unbuilt") and os.path.exists(p["unbuilt"]):
        problems.append("%s still exists: delete it (git rm)" % p["unbuilt"])
    if os.path.exists(pending):
        problems.append("%s still exists: move it (git mv %s %s)" % (pending, pending, final))
    item(1, problems)

    r = subprocess.run(["git", "show", "%s:%s" % (base, pending)], capture_output=True)
    if r.returncode:
        problems = ["the base %s has no %s: the piece's test task is not merged in the base of this branch" % (base[:12], pending)]
    elif not os.path.exists(final):
        problems = ["%s does not exist" % final]
    elif open(final, "rb").read() != r.stdout:
        problems = ["%s differs from %s of the base: the test is moved unchanged (git diff --no-index, or "
                    "git show %s:%s > %s)" % (final, pending, base[:12], pending, final)]
    else:
        problems = []
    ok2 = item(2, problems)

    problems = []
    for s in p["sources"]:
        if os.path.exists(s) and "RT_CORE_UNIMPLEMENTED" in strip_comments(open(s, errors="replace").read()) and not p.get("keeps_stub"):
            problems.append("%s still contains RT_CORE_UNIMPLEMENTED" % s)
    item(3, problems)

    if not suite(args, 4) or not ok2:
        item(5, ["not run: items 2 and 4 must pass first"])
        return
    tests, problems = layout(open(final, errors="replace").read(), p)
    notes = []
    rc, out, err = run(["build/runtime/rt-test", "--report"] + ["core/" + name for name, _ in tests], timeout=1800)
    for n in table.plan_cases(p):
        rep = re.search(r"^#rt case (\d+) (\d+) %d: " % n, out, re.M)
        if not rep:
            problems.append("case %d did not run" % n)
        elif int(rep.group(1)) == 0 or int(rep.group(2)):
            problems.append("case %d: %s checks, %s failed" % (n, rep.group(1), rep.group(2)))
    if rc:
        problems.append("the piece's tests failed (%s):\n%s" % (rc, tail(out + err, 15)))
    notes.append("%d cases of the plan in %d TESTs; %d refused" % (len(table.plan_cases(p)), len(tests), len(problems)))
    item(5, problems, notes)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kind", choices=("test", "body"))
    ap.add_argument("piece")
    ap.add_argument("--base-ref", default="wip/lifter-v0")
    ap.add_argument("--base")
    ap.add_argument("--make", default="make")
    args = ap.parse_args()
    p = table.piece(args.piece)
    if not os.path.isdir("runtime/tests/core") or not os.path.exists(".git"):
        sys.exit("run this from the root of the worktree")
    (check_test if args.kind == "test" else check_body)(p, args)
    print("core-%s-check %s: %s" % (args.kind, p["id"], "FAILED (items %s)" % ", ".join(map(str, failed)) if failed else "passed"))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
