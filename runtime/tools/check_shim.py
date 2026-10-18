#!/usr/bin/env python3
"""check_shim.py: the "done" check for one shim (docs/RUNTIME.md section 10.5).

Run it from the repository root, normally through make:

    make shim-check SHIM=<lib>/<Name>

or by hand:

    .venv/bin/python runtime/tools/check_shim.py <lib>/<Stem> [--base COMMIT] [--base-ref REF]
                                                 [--make CMD] [--skip-build]

<lib>/<Stem> names the shim runtime/shims/<lib>/<Stem>.c and its test
runtime/tests/shims/<lib>/<Stem>.c.  <Stem> is the function name, or ord<number>_<name> for an
import by ordinal.  One line per item is printed, then indented detail lines for failures and
notes, then one summary line.  Every item is always run and printed.

    [1] only the two permitted files changed (the shim and its test), against the merge base
    [2] names, SHIM line, header tags and the import list agree; the test file is well formed
    [3] lint: forbidden types, direct allocation, path handling, errno, includes, shim-to-shim calls
    [4] `make rt` builds the runtime with no warnings (the build uses -Werror)
    [5] `make rt-test-build`, then the shim's test passes through the guest ABI
    [6] the test has a T_CASE that ran and checked something for every RT-CASES entry, and
        checks the failure codes the shim names
    [7] for status partial: every RT-TRAPS entry is reached by an RT_UNIMPLEMENTED call
    [8] the whole runtime test suite (build/runtime/rt-test) still passes

Exit status: 0 if every item passed, 1 if any failed or was skipped (--skip-build is a
development aid and skipped items count as failures), 2 for a usage error.

The parser is runtime/tools/shimdef.py, shared with gen_shims.py.  Single process, standard
library only: it runs make, git and the test binary as subprocesses, each with a timeout.
"""

import argparse
import collections
import os
import re
import shlex
import signal
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shimdef                                                     # noqa: E402
from shimdef import ShimError, strip_comments, strip_strings        # noqa: E402

TIMEOUT = 900                                   # seconds, for every subprocess
DEFAULT_BASE_REF = "wip/lifter-v0"
BOOTSTRAP_MARKER = "runtime/tools/check_shim.py"   # exists at the base = the runtime existed there
TEST_BINARY = "build/runtime/rt-test"
TYPES_DEF = "runtime/tools/types.def"
COM_TYPES_DEF = "runtime/com/types.def"             # the Direct3D names (runtime/com/gen_desc.py), read after it
IMPORTS_DEF = "runtime/tools/imports.def"
IMPORTS_EXTRA_DEF = "runtime/tools/imports_extra.def"

TITLES = {
    1: "only the two permitted files changed",
    2: "names, SHIM line, header tags and import list agree",
    3: "lint",
    4: "the runtime builds with no warnings",
    5: "the shim's test passes through the guest ABI",
    6: "the test covers the declared cases",
    7: "every RT-TRAPS entry is reached by an RT_UNIMPLEMENTED call",
    8: "the whole runtime test suite still passes",
}

# ---- planner-owned lists --------------------------------------------------------------------
# The include lists and the host-function lists below belong to the planner.  A worker never
# edits them; a shim that needs a header or a call that is not allowed stops and says so in the
# issue (docs/RUNTIME.md section 10.1).

INCLUDE_POLICY = {
    "shim": {
        "what": "a shim file",
        "required": "rt/shim.h",
        "quoted": ("rt/shim.h", "rt/handle.h", "rt/vm.h", "rt/heap.h", "rt/lock.h", "rt/thread.h",
                   "rt/module.h", "rt/path.h", "rt/machine.h", "rt/time.h", "rt/log.h", "rt/env.h",
                   "rt/locale.h", "rt/sync.h", "rt/registry.h", "rt/file.h", "rt/crash.h", "rt/unwind.h",
                   "rt/net.h", "rt/window.h", "rt/gdi.h", "rt/com.h", "rt/nulldev.h", "rt/bcrypt.h", "rt/rawinput.h"),
        "quoted_patterns": (r"rt/win/[A-Za-z0-9_]+\.h",),
        "system": ("string.h", "stdint.h", "stddef.h", "stdbool.h", "limits.h", "errno.h",
                   "unistd.h", "fcntl.h", "sys/stat.h", "sys/time.h", "time.h", "math.h"),
    },
    "test": {
        "what": "a test file",
        "required": "rt/test.h",
        "quoted": ("rt/test.h",),
        "quoted_patterns": (r"rt/[A-Za-z0-9_]+\.h", r"rt/win/[A-Za-z0-9_]+\.h"),
        "system": ("string.h", "stdint.h", "stddef.h", "stdbool.h", "limits.h", "math.h",
                   "stdio.h", "stdlib.h", "unistd.h", "fcntl.h", "sys/stat.h"),
    },
}

ALLOC_FUNCS = ("malloc", "calloc", "realloc", "free", "strdup", "strndup", "valloc",
               "aligned_alloc", "posix_memalign", "mmap", "munmap", "alloca")
HOST_PATH_FUNCS = ("open", "fopen", "freopen", "stat", "lstat", "access", "unlink", "remove",
                   "rename", "mkdir", "rmdir", "opendir", "chdir", "getcwd", "realpath", "truncate",
                   "readlink", "symlink", "link", "chmod", "utimes", "statfs")
ERRNO_NAMES = ("PERM NOENT SRCH INTR IO NXIO 2BIG NOEXEC BADF CHILD AGAIN NOMEM ACCES FAULT BUSY "
               "EXIST XDEV NODEV NOTDIR ISDIR INVAL NFILE MFILE NOTTY TXTBSY FBIG NOSPC SPIPE ROFS "
               "MLINK PIPE DOM RANGE DEADLK NAMETOOLONG NOLCK NOSYS NOTEMPTY LOOP WOULDBLOCK "
               "TIMEDOUT NOTSUP OVERFLOW DQUOT").split()

Result = collections.namedtuple("Result", "status title details")     # status: PASS FAIL SKIP
Target = collections.namedtuple("Target", "lib stem shim_path test_path")
Run = collections.namedtuple("Run", "rc out err error")                # error: why it did not run to the end
Report = collections.namedtuple("Report", "cases calls result")        # cases: [(checks, failures, text)]

ARG_RE = re.compile(r"([a-z][a-z0-9_]*)/([A-Za-z_][A-Za-z0-9_]*)\Z")
LITERAL = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')
STRING_LIT = r'"(?:\\.|[^"\\\n])*"'
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(?:"([^"\n]*)"|<([^>\n]*)>)', re.M)
INCLUDE_ANY_RE = re.compile(r"^[ \t]*#[ \t]*include\b.*$", re.M)


# ---- small helpers --------------------------------------------------------------------------

def parse_target(arg):
    m = ARG_RE.match(arg)
    if not m:
        return None
    lib, stem = m.groups()
    return Target(lib, stem, "runtime/shims/%s/%s.c" % (lib, stem),
                  "runtime/tests/shims/%s/%s.c" % (lib, stem))


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def norm(text):
    return " ".join(text.split())


def tail(text, n):
    lines = text.rstrip("\n").split("\n") if text.strip() else []
    return lines[-n:]


def capped(items, limit=20):
    """At most `limit` items, then '... and N more'."""
    out = list(items[:limit])
    if len(items) > limit:
        out.append("... and %d more" % (len(items) - limit))
    return out


def finish(n, problems, notes=()):
    """Item n's result: FAIL if there is any problem; notes are printed but never fail an item."""
    return Result("FAIL" if problems else "PASS", TITLES[n], list(problems) + list(notes))


def finding(rel, line, text):
    return (line, "%s:%d: %s" % (rel, line, text))


def sorted_findings(items):
    return [text for _, text in sorted(set(items))]


def read_text(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def run_cmd(argv, cwd=None, merge=False, timeout=None):
    """Run a command to the end.  Returns Run(rc, out, err, error); `error` is set (and rc is
    None) if the command could not start or was killed after the timeout."""
    timeout = timeout or TIMEOUT
    try:
        p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT if merge else subprocess.PIPE,
                             text=True, errors="replace", start_new_session=True)
    except OSError as e:
        return Run(None, "", "", "cannot run '%s': %s" % (argv[0], e))
    try:
        out, err = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            p.kill()
        out, err = p.communicate()
        return Run(None, out or "", err or "", "timed out after %d seconds and was killed: %s" % (
            timeout, " ".join(shlex.quote(a) for a in argv)))
    return Run(p.returncode, out or "", err or "", None)


def git(args, cwd=None):
    return run_cmd(["git"] + list(args), cwd=cwd)


def failed(run):
    return run.error is not None or run.rc != 0


# How a fatal report of the runtime ends a process (runtime/include/rt/crash.h, THE QUIET END):
# by SIGABRT, which subprocess shows as -6; or, where the program asked for the quiet end (a
# child a test forked, a driver's run), by exit status 134 with no crash report of macOS.
# A FAILED test's own process still dies of SIGABRT, and the runner says so in its FAIL line
# and exits with status 1: neither value is ever the runner's own status when all is well.
QUIET_ABORT_STATUS = 128 + int(signal.SIGABRT)      # RT_REPORT_QUIET_STATUS


def aborted(rc):
    """True for both ends of a fatal report: killed by SIGABRT, and the quiet end (exit status 134)."""
    return rc is not None and rc in (-int(signal.SIGABRT), QUIET_ABORT_STATUS)


def status_text(rc):
    """'exit status 2'; 'killed by signal 6 (SIGABRT)' for the negative status of a killed process;
    and the quiet end of a fatal report, exit status 134, in words that say it is the same event."""
    if rc is not None and rc < 0:
        try:
            return "killed by signal %d (%s)" % (-rc, signal.Signals(-rc).name)
        except ValueError:
            return "killed by signal %d" % -rc
    if rc == QUIET_ABORT_STATUS:
        return "exit status %d (the quiet end of a fatal report: 128 + signal %d, SIGABRT)" % (rc, int(signal.SIGABRT))
    return "exit status %s" % rc


def selftest():
    """check_shim.py --selftest: how the two check tools read the status of a program they ran.
    The expected values: Python's subprocess documents a negative returncode -N for a child
    killed by signal N; 134 is RT_REPORT_QUIET_STATUS of runtime/include/rt/crash.h."""
    bad = []

    def want(what, got, expected):
        if got != expected:
            bad.append("%s: got %r, expected %r" % (what, got, expected))

    crash_h = read_text(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "include", "rt", "crash.h")) or ""
    m = re.search(r"^#define\s+RT_REPORT_QUIET_STATUS\s+(\d+)\s*$", crash_h, re.M)
    want("RT_REPORT_QUIET_STATUS in rt/crash.h", m and int(m.group(1)), QUIET_ABORT_STATUS)
    want("QUIET_ABORT_STATUS", QUIET_ABORT_STATUS, 134)
    want("status_text(0)", status_text(0), "exit status 0")
    want("status_text(2)", status_text(2), "exit status 2")
    want("status_text(-6)", status_text(-6), "killed by signal 6 (SIGABRT)")
    want("status_text(-11)", status_text(-11), "killed by signal 11 (SIGSEGV)")
    want("status_text(-200)", status_text(-200), "killed by signal 200")
    want("status_text(134)", status_text(134), "exit status 134 (the quiet end of a fatal report: 128 + signal 6, SIGABRT)")
    want("status_text(133)", status_text(133), "exit status 133")
    want("status_text(None)", status_text(None), "exit status None")
    for rc, yes in ((-6, True), (134, True), (0, False), (1, False), (6, False), (-11, False), (139, False), (None, False)):
        want("aborted(%r)" % rc, aborted(rc), yes)
    for rc in (-6, 134, 1):                             # every one of them is a failure of the program that was run
        want("failed(rc=%r)" % rc, failed(Run(rc, "", "", None)), True)
    want("failed(rc=0)", failed(Run(0, "", "", None)), False)
    # Through run_cmd, with real processes that leave no crash report: one that exits as the
    # quiet end does (the runtime's own tests prove that a report ends so: tests/core/report_quiet.c),
    # one killed by a signal that macOS does not write up.
    py = sys.executable
    quiet = run_cmd([py, "-c", "import os; os._exit(%d)" % QUIET_ABORT_STATUS])
    want("a process that exits with status 134", quiet.rc, QUIET_ABORT_STATUS)
    want("why() of it", why(quiet), status_text(QUIET_ABORT_STATUS))
    want("aborted() of it", aborted(quiet.rc), True)
    killed = run_cmd([py, "-c", "import os, signal; os.kill(os.getpid(), signal.SIGKILL)"])
    want("a process killed by SIGKILL", killed.rc, -int(signal.SIGKILL))
    want("status_text of it", status_text(killed.rc), "killed by signal 9 (SIGKILL)")
    for line in bad:
        print("check_shim selftest: FAILED: " + line)
    print("check_shim selftest: %s" % ("%d failure(s)" % len(bad) if bad else "ok"))
    return 1 if bad else 0


def why(run):
    """One line saying why a git command did not succeed."""
    if run.error:
        return run.error
    text = run.err.strip() or run.out.strip()
    return status_text(run.rc) + ((": " + text.split("\n")[0]) if text else "")


# ---- item 1: only the two permitted files changed -------------------------------------------

def porcelain_paths(data):
    """Paths from `git status --porcelain -z`.  A rename or copy is 'XY new\\0old\\0': both count."""
    parts = data.split("\0")
    paths, i = [], 0
    while i < len(parts):
        entry = parts[i]
        i += 1
        if len(entry) < 4:
            continue
        paths.append(entry[3:])
        if (entry[0] in "RC" or entry[1] in "RC") and i < len(parts) and parts[i]:
            paths.append(parts[i])
            i += 1
    return paths


def nul_paths(data):
    return [p.strip("\n") for p in data.split("\0") if p.strip("\n")]


def working_tree_changes(cwd):
    r = git(["status", "--porcelain", "-z", "--untracked-files=all"], cwd)
    if failed(r):
        return None, "git status failed: " + why(r)
    return porcelain_paths(r.out), None


def item1_changes(target, base, base_ref, cwd=None, family=()):
    """Only the shim and its test may differ from the base (RUNTIME.md section 10.1).  A worker
    who was given a family of shims names the others with --family; their files may differ too."""
    permitted = {target.shim_path, target.test_path}
    for sibling in family:
        permitted.update((sibling.shim_path, sibling.test_path))
    hint = ("pass RT_BASE=<commit> (a commit your branch was cut from) or RT_BASE_REF=<branch> "
            "(the branch it was cut from) to make")
    if base:
        r = git(["rev-parse", "--verify", "--quiet", base + "^{commit}"], cwd)
        if failed(r) or not r.out.strip():
            return finish(1, ["git does not know the base commit '%s'; %s" % (base, hint)])
        base_hash = r.out.strip()
    else:
        r = git(["merge-base", "HEAD", base_ref], cwd)
        if failed(r) or not r.out.strip():
            return finish(1, ["cannot find the base: 'git merge-base HEAD %s' failed (%s); %s" % (
                base_ref, why(r), hint)])
        base_hash = r.out.strip()
    r = git(["rev-parse", "--short", base_hash], cwd)
    short = r.out.strip() if not failed(r) and r.out.strip() else base_hash[:9]

    marker = git(["cat-file", "-e", "%s:%s" % (base_hash, BOOTSTRAP_MARKER)], cwd)
    if marker.error:
        return finish(1, ["git failed: " + marker.error])
    wt, err = working_tree_changes(cwd)
    if err:
        return finish(1, [err])
    allowed = "%s and %s" % (target.shim_path, target.test_path)
    if family:
        allowed += " (and the two files of each other shim of the family: %s)" % ", ".join(
            "%s/%s" % (t.lib, t.stem) for t in family)
    undo = ("undo those changes (restore modified files from the base, delete new ones); if the shim "
            "cannot be finished without them, stop and say so in the issue (docs/RUNTIME.md section 10.1)")

    if marker.rc == 0:                                              # normal mode
        r = git(["diff", "--name-only", "-z", "--no-renames", base_hash, "HEAD"], cwd)
        if failed(r):
            return finish(1, ["git diff %s HEAD failed: %s" % (short, why(r))])
        origin = {}
        for p in nul_paths(r.out):
            origin.setdefault(p, []).append("committed since %s" % short)
        for p in wt:
            origin.setdefault(p, []).append("uncommitted")
        bad = sorted(p for p in origin if p not in permitted)
        if not bad:
            return finish(1, [])
        listing = ["%s  (%s)" % (p, ", ".join(sorted(set(origin[p])))) for p in bad]
        return finish(1, ["%d path(s) other than %s changed; %s:" % (len(bad), allowed, undo)]
                      + capped(listing))

    # Bootstrap mode: the base has no runtime, so a diff against it contains the whole runtime.
    problems = []
    bad_wt = sorted(set(p for p in wt if p not in permitted))
    if bad_wt:
        problems.append("%d uncommitted or untracked path(s) other than %s; %s:" % (
            len(bad_wt), allowed, undo))
        problems += capped(bad_wt)
    r = git(["rev-list", "%s..HEAD" % base_hash], cwd)
    if failed(r):
        return finish(1, problems + ["git rev-list %s..HEAD failed: %s" % (short, why(r))])
    offenders = []
    for commit in r.out.split():
        s = git(["show", "--pretty=format:", "--name-only", "-z", "--no-renames", commit], cwd)
        if failed(s):
            return finish(1, problems + ["git show %s failed: %s" % (commit[:9], why(s))])
        names = set(nul_paths(s.out))
        if not (names & permitted):
            continue
        extra = sorted(names - permitted)
        if extra:
            h = git(["log", "-1", "--format=%h%x09%s", commit], cwd)
            who = commit[:9]
            if not failed(h) and "\t" in h.out:
                who = '%s "%s"' % tuple(h.out.strip().split("\t", 1))
            offenders.append("commit %s also touches %s" % (who, ", ".join(capped(extra, 5))))
    if offenders:
        problems.append("%d commit(s) since %s touch your files and other paths too; %s (a commit "
                        "that touches the shim or its test may touch nothing else):" % (
                            len(offenders), short, "split the other changes out of them"))
        problems += capped(offenders)
    note = ("note: bootstrap mode: the base %s has no runtime, so the check is per commit (commits "
            "that touch the shim's two files) plus the working tree" % short)
    return finish(1, problems, [note])


# ---- item 2: names, SHIM line, header tags, import list ---------------------------------------

def load_imports(main_path, extra_path):
    """-> [(lib_id, name, ordinal or 0, 'file:line')].  A missing extra file means no rows."""
    rows = []
    for path, optional in ((main_path, False), (extra_path, True)):
        text = read_text(path)
        if text is None:
            if optional and not os.path.exists(path):
                continue
            raise ShimError("%s cannot be read; it is generated or kept by the planner, so report "
                            "this instead of fixing it" % path)
        for n, raw in enumerate(text.split("\n"), 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            f = line.split()
            if len(f) < 3 or not (f[2] == "-" or f[2].isdigit()):
                raise ShimError("%s:%d: expected '<library> <name> <ordinal or -> <wave> <mark>'; "
                                "the file is planner-owned, so report this" % (path, n))
            rows.append((shimdef.lib_id(f[0]), f[1], 0 if f[2] == "-" else int(f[2]), "%s:%d" % (path, n)))
    return rows


def item2_names(target, shim, error, test_text, imports_paths):
    problems, notes = [], []
    if error:
        problems.append(error)
    if shim:
        try:
            rows = load_imports(*imports_paths)
        except ShimError as e:
            problems.append(str(e))
        else:
            mine = [r for r in rows if r[0] == shim.lib and r[1] == shim.name]
            ordinals = sorted(set(r[2] for r in mine if r[2]))
            if not mine:
                problems.append(
                    "%s!%s is not in list (b) of docs/findings/24-import-surface.md (runtime/tools/imports.def) "
                    "and was not added by the planner (runtime/tools/imports_extra.def)" % (shim.lib, shim.name))
            elif ordinals and shim.kind != "SHIM_ORD":
                problems.append(
                    "%s:%d: %s!%s is imported by ordinal %s (%s), so the shim must be declared with "
                    "SHIM_ORD(%s, %d, ...), not %s" % (shim.rel, shim.line, shim.lib, shim.name,
                                                        "/".join(map(str, ordinals)), mine[0][3], shim.lib,
                                                        ordinals[0], shim.kind))
            elif ordinals and shim.ordinal not in ordinals:
                problems.append(
                    "%s:%d: SHIM_ORD declares ordinal %d but %s!%s is imported as ordinal %s (%s)" % (
                        shim.rel, shim.line, shim.ordinal, shim.lib, shim.name,
                        "/".join(map(str, ordinals)), mine[0][3]))
            if shim.status in ("absent", "advisory"):
                notes.append("note: status %s needs the planner's sign-off recorded in the issue "
                             "(docs/RUNTIME.md section 9.2)" % shim.status)

    tpath = target.test_path
    if test_text is None:
        problems.append("%s does not exist: write the test (copy a finished test as the pattern)" % tpath)
    else:
        code = strip_comments(test_text)
        if "rt/test.h" not in [m.group(1) for m in INCLUDE_RE.finditer(code)]:
            problems.append('%s: the test must contain #include "rt/test.h"' % tpath)
        if shim:
            stripped = strip_strings(code)
            tests = list(re.finditer(r"\bTEST\s*\(", stripped))
            want = "TEST(%s, %s)" % (shim.lib, shim.name)
            if not tests:
                problems.append("%s: no TEST(...) found; the test file needs exactly one, %s" % (tpath, want))
            elif len(tests) > 1:
                problems.append("%s:%d: %d TEST(...) declarations; a test file has exactly one, %s" % (
                    tpath, line_of(stripped, tests[1].start()), len(tests), want))
            else:
                m = re.match(r"\bTEST\s*\(\s*([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)\s*\)", stripped[tests[0].start():])
                if not m or (m.group(1), m.group(2)) != (shim.lib, shim.name):
                    got = norm(code[tests[0].start():tests[0].start() + 80].split(")")[0]) + ")"
                    problems.append("%s:%d: found %s but it must be %s (the library and the function "
                                    "name as the SHIM line spells them, not the file name)" % (
                                        tpath, line_of(stripped, tests[0].start()), got, want))
    return finish(2, problems, notes)


# ---- item 3: lint -----------------------------------------------------------------------------

def call_re(names):
    """A call to one of `names`: whole word, not a member access, followed by '('."""
    return re.compile(r"(?<![\w.])(?<!->)(%s)\s*\(" % "|".join(names))


def matching_close(text, open_idx):
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


def statement_end(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        elif text[i] == ";" and depth <= 0:
            return i
    return len(text) - 1


FORBIDDEN_TYPE_WHY = ("its size differs between x64 Windows and arm64 macOS; use the Windows types of "
                      "rt/wintypes.h (LONG, ULONG, WCHAR, SIZE_T, ...), docs/RUNTIME.md section 3.3")


def lint_forbidden_types(rel, shim, stripped):
    """long, wchar_t, size_t on the SHIM line and inside typedef / struct / union declarations."""
    out = []
    word = re.compile(r"\b(long|wchar_t|size_t)\b")
    for m in word.finditer(shim.decl):
        out.append(finding(rel, shim.line, "'%s' in the SHIM line: %s" % (m.group(1), FORBIDDEN_TYPE_WHY)))
    where = {}                                  # position of the word -> kind of declaration
    for m in re.finditer(r"\btypedef\b", stripped):
        a, b = m.start(), statement_end(stripped, m.end()) + 1
        for w in word.finditer(stripped, a, b):
            where[w.start()] = "typedef"
    for m in re.finditer(r"\b(struct|union)\b(?:\s+[A-Za-z_]\w*)?\s*\{", stripped):
        a, b = m.start(), matching_close(stripped, m.end() - 1) + 1
        for w in word.finditer(stripped, a, b):
            where[w.start()] = "%s body" % m.group(1)
    for pos, what in where.items():
        out.append(finding(rel, line_of(stripped, pos), "'%s' in a %s: %s" % (
            word.match(stripped, pos).group(1), what, FORBIDDEN_TYPE_WHY)))
    return out


def lint_allocation(rel, stripped):
    out = []
    for m in call_re(ALLOC_FUNCS).finditer(stripped):
        out.append(finding(rel, line_of(stripped, m.start()),
                           "direct call to %s(): memory the guest sees comes from rt_galloc / rt_gfree "
                           "(rt/heap.h); host-private state belongs in runtime/core" % m.group(1)))
    return out


def decode_escapes(s):
    return re.sub(r"\\(.)", lambda m: m.group(1), s)


def lint_paths(rel, code, stripped):
    out = []
    for m in LITERAL.finditer(code):
        lit, line = m.group(0), line_of(code, m.start())
        inner = lit[1:-1]
        if lit[0] == "'":
            if inner in ("\\\\", "/"):
                out.append(finding(rel, line, "the character literal %s: a shim must not parse paths; "
                                              "convert them with rt_path_to_host_a / rt_path_to_host_w "
                                              "and the other rt_path_* functions (rt/path.h)" % lit))
            continue
        value = decode_escapes(inner)
        if (re.match(r"[A-Za-z]:[\\/]", value) or re.match(r"/[A-Za-z]", value)
                or value.startswith("\\\\")):
            out.append(finding(rel, line, "the string %s looks like an absolute path: a shim does not "
                                          "build or hard-code paths; the virtual C: drive and the host "
                                          "mapping live in rt_path_* (rt/path.h)" % lit))
    if not re.search(r"\brt_path_to_host_[aw]\s*\(", stripped):
        for m in call_re(HOST_PATH_FUNCS).finditer(stripped):
            out.append(finding(rel, line_of(stripped, m.start()),
                               "call to %s() but the file never calls rt_path_to_host_a() or "
                               "rt_path_to_host_w(): a guest path must be converted to a host path first "
                               "(rt/path.h)" % m.group(1)))
    return out


def lint_errno(rel, stripped):
    out = []
    text = INCLUDE_ANY_RE.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), stripped)
    text = re.sub(r"(rt_win32_from_errno\s*\(\s*)errno(\s*[,)])",
                  lambda m: m.group(1) + " " * 5 + m.group(2), text)
    for m in re.finditer(r"\berrno\b", text):
        out.append(finding(rel, line_of(text, m.start()),
                           "'errno' used directly: map a host error to a Windows error code only with "
                           "rt_win32_from_errno(errno) and do not read, test or save errno yourself"))
    for m in re.finditer(r"\bE(?:%s)\b" % "|".join(ERRNO_NAMES), text):
        out.append(finding(rel, line_of(text, m.start()),
                           "errno constant %s: the mapping from host errors to Windows error codes lives "
                           "in rt_win32_from_errno; call it instead of testing or choosing by errno value" % m.group(0)))
    return out


def allowed_includes(pol):
    parts = ['"%s"' % q for q in pol["quoted"]]
    parts += ['"%s"' % p.replace("[A-Za-z0-9_]+", "<name>").replace("\\.", ".") for p in pol["quoted_patterns"]]
    parts += ["<%s>" % s for s in pol["system"]]
    return " ".join(parts)


def lint_includes(rel, code, policy_name):
    pol = INCLUDE_POLICY[policy_name]
    out, seen = [], []
    for m in INCLUDE_RE.finditer(code):
        quoted, system = m.group(1), m.group(2)
        line = line_of(code, m.start())
        seen.append(quoted)
        if quoted is not None:
            ok = quoted in pol["quoted"] or any(re.fullmatch(p, quoted) for p in pol["quoted_patterns"])
            shown = '"%s"' % quoted
        else:
            ok = system in pol["system"]
            shown = "<%s>" % system
        if not ok:
            first = not any("is not allowed" in t for _, t in out)
            out.append(finding(rel, line, "include %s is not allowed in %s%s.  If you really need it, stop "
                                          "and say so in the issue; do not work around it" % (
                                              shown, pol["what"],
                                              " (allowed: %s)" % allowed_includes(pol) if first
                                              else " (the allowed list is in the first finding)")))
    parsed = {m.start() for m in INCLUDE_RE.finditer(code)}
    for m in INCLUDE_ANY_RE.finditer(code):
        if m.start() not in parsed:
            out.append(finding(rel, line_of(code, m.start()),
                               "unrecognised #include form: write #include \"...\" or #include <...> with a literal name"))
    if pol["required"] not in seen:
        out.append(finding(rel, 1, 'the file must contain #include "%s"' % pol["required"]))
    return out


def lint_shim_calls(rel, stripped):
    out = []
    for m in re.finditer(r"(?<![\w.])((?:impl|shim)_[a-z][a-z0-9_]*_[A-Za-z_]\w*)\s*\(", stripped):
        out.append(finding(rel, line_of(stripped, m.start()),
                           "call to %s(): a shim does not call another shim; shared logic lives in "
                           "runtime/core (docs/RUNTIME.md section 10.2).  If this is your own helper, "
                           "rename it so it does not start with impl_ or shim_" % m.group(1)))
    return out


def lint_shim(shim):
    """Every lint finding for the shim file, as 'path:line: text', sorted by line."""
    rel = shim.rel
    code = strip_comments(shim.source)
    stripped = strip_strings(code)
    found = []
    found += lint_forbidden_types(rel, shim, stripped)
    found += lint_allocation(rel, stripped)
    found += lint_paths(rel, code, stripped)
    found += lint_errno(rel, stripped)
    found += lint_includes(rel, code, "shim")
    found += lint_shim_calls(rel, stripped)
    return sorted_findings(found)


def lint_test_includes(test_path, test_text):
    return sorted_findings(lint_includes(test_path, strip_comments(test_text), "test"))


def item3_lint(target, shim, test_text):
    if shim is None:
        return Result("FAIL", TITLES[3], ["not checked: the shim file could not be parsed"])
    problems = lint_shim(shim)
    if test_text is not None:
        problems += lint_test_includes(target.test_path, test_text)
    return finish(3, problems)


# ---- item 4: the runtime builds ---------------------------------------------------------------

def make_failure(run, what, argv, lines):
    out = [what + (": " + run.error if run.error else " (%s)" % status_text(run.rc))]
    last = tail(run.out + run.err, lines)
    if last:
        out.append("last %d lines of '%s':" % (lines, " ".join(argv)))
        out += last
    return out


def item4_build(make_argv, state):
    argv = make_argv + ["rt"]
    run = run_cmd(argv, merge=True)
    if failed(run):
        state["build_failed"] = True
        return finish(4, make_failure(run, "the runtime does not build", argv, 30))
    return finish(4, [])


def ensure_test_build(make_argv, state):
    if "test_build" not in state:
        state["test_build"] = run_cmd(make_argv + ["rt-test-build"], merge=True)
    return state["test_build"]


# ---- item 5: the shim's test passes -----------------------------------------------------------

REPORT_CASE = re.compile(r"#rt case (\d+) (\d+) (.*)\Z")
REPORT_CALLS = re.compile(r"#rt calls (\S+) (\d+)\Z")
REPORT_RESULT = re.compile(r"#rt result (pass|fail) (\d+) (\d+)\Z")


def parse_report(stdout):
    cases, calls, result = [], {}, None
    for line in stdout.split("\n"):
        line = line.rstrip("\r")
        m = REPORT_CASE.match(line)
        if m:
            cases.append((int(m.group(1)), int(m.group(2)), norm(m.group(3))))
        m = REPORT_CALLS.match(line)
        if m:
            calls[m.group(1)] = calls.get(m.group(1), 0) + int(m.group(2))
        m = REPORT_RESULT.match(line)
        if m:
            result = (m.group(1), int(m.group(2)), int(m.group(3)))
    return Report(cases, calls, result)


def test_output_tail(run, lines):
    """The test's stderr (its failure messages), then its stdout without the report lines that
    only say 'all fine', last `lines` lines."""
    keep = []
    for line in run.out.split("\n"):
        if line.startswith("#rt test ") or line.startswith("#rt calls "):
            continue
        m = REPORT_CASE.match(line)
        if m and m.group(2) == "0":
            continue
        keep.append(line)
    return tail("\n".join(l for l in run.err.split("\n") + keep if l.strip()), lines)


def split_call(stripped, plain, open_idx):
    """The arguments of the call whose '(' is at open_idx: ([text of each argument], index of the
    closing ')') or None if the parentheses never close.  `stripped` is `plain` with string
    contents blanked (same length), so commas in strings do not split."""
    depth, start, args = 0, open_idx + 1, []
    for i in range(open_idx, len(stripped)):
        ch = stripped[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                args.append(plain[start:i])
                return args, i
        elif ch == "," and depth == 1:
            args.append(plain[start:i])
            start = i + 1
    return None


def trap_test_calls_shim(test_text, shim):
    """True if some T_TRAPS(...) statement in the test contains CALL(<lib>, <name>."""
    code = strip_comments(test_text)
    stripped = strip_strings(code)
    want = re.compile(r"\bCALL\s*\(\s*%s\s*,\s*%s\b" % (re.escape(shim.lib), re.escape(shim.name)))
    for m in re.finditer(r"\bT_TRAPS\s*\(", stripped):
        got = split_call(stripped, code, m.end() - 1)
        if got and want.search(got[0][0]):
            return True
    return False


def blocked_reason(state):
    """Why the dynamic items cannot run, or None."""
    if state.get("build_failed"):
        return "the build failed (item 4)"
    return state.get("dyn_blocked")


def test_build_problems(make_argv, state):
    """Build the test program (once).  -> list of problems, empty if build/runtime/rt-test exists."""
    build = ensure_test_build(make_argv, state)
    argv = make_argv + ["rt-test-build"]
    if failed(build):
        state["dyn_blocked"] = "the test build failed (item 5)"
        return make_failure(build, "the test program does not build", argv, 30)
    if not os.path.isfile(TEST_BINARY):
        state["dyn_blocked"] = "%s does not exist after the test build (item 5)" % TEST_BINARY
        return ["'%s' succeeded but %s does not exist" % (" ".join(argv), TEST_BINARY)]
    return []


def item5_test(shim, test_text, make_argv, state):
    if shim is None:
        return Result("FAIL", TITLES[5], ["not checked: the shim file could not be parsed"])
    if blocked_reason(state):
        return Result("FAIL", TITLES[5], ["not run: " + blocked_reason(state)])
    problems = test_build_problems(make_argv, state)
    if problems:
        return finish(5, problems)

    sid = "%s/%s" % (shim.lib, shim.name)
    run = run_cmd([TEST_BINARY, "--report", sid])
    shown = ["last 40 lines of the test's output (%s --report %s):" % (TEST_BINARY, sid)]
    problems = []
    report = parse_report(run.out)
    state["report"] = report if (report.result or report.cases) else None
    if run.error:
        problems.append("the test did not finish: " + run.error)
    else:
        res = report.result
        if run.rc == 2:
            problems.append("the runner has no test named %s (exit status 2): check that the test file "
                            "contains TEST(%s, %s) and that it was built" % (sid, shim.lib, shim.name))
        elif run.rc != 0:
            problems.append("the test failed (%s%s)" % (
                status_text(run.rc), ", %d check(s), %d failure(s)" % (res[1], res[2]) if res else ""))
        if run.rc != 2 and res is None:
            problems.append("no '#rt result' line: the test did not run to the end (it crashed, aborted or "
                            "called exit); its output is below")
        elif run.rc == 0 and (res[0] != "pass" or res[2] != 0):
            problems.append("the test exited with status 0 but reports '#rt result %s %d %d'" % res)
        elif run.rc == 0 and res[1] < 1:
            problems.append("the test made no check")
        if shim.status == "trap":
            if not trap_test_calls_shim(test_text or "", shim):
                problems.append("status trap: the call happens in a child process and is not counted, so the "
                                "test must contain T_TRAPS(CALL(%s, %s, ...), \"...\"); none found" % (
                                    shim.lib, shim.name))
        elif run.rc == 0 and report.calls.get(sid, 0) < 1:
            problems.append("the test never called the shim through CALL: there is no '#rt calls %s <n>' "
                            "line; call it with CALL(%s, %s, ...), not by its C name" % (sid, shim.lib, shim.name))
    if problems:
        last = test_output_tail(run, 40)
        return finish(5, problems + (shown + last if last else []))
    return finish(5, [])


# ---- item 6: the test covers the declared cases -----------------------------------------------

def test_case_texts(test_text):
    """Texts of the T_CASE("...") calls, in the comment-stripped test, whitespace collapsed."""
    code = strip_comments(test_text)
    out = []
    for m in re.finditer(r"\bT_CASE\s*\(\s*((?:%s\s*)+)\)" % STRING_LIT, code):
        out.append(norm("".join(re.findall(r'"((?:\\.|[^"\\\n])*)"', m.group(1)))))
    return out


def error_code_problems(shim, test_text):
    code = strip_comments(shim.source)
    stripped = strip_strings(code)
    tcode = strip_comments(test_text)
    out = []
    first = {}
    for m in re.finditer(r"\bERROR_[A-Z0-9_]+\b", stripped):
        if m.group(0) != "ERROR_SUCCESS":
            first.setdefault(m.group(0), line_of(stripped, m.start()))
    for ident, line in sorted(first.items(), key=lambda kv: (kv[1], kv[0])):
        pat = (r"\bT_EQ\s*\(\s*(?:T_LAST_ERROR\s*\(\s*\)\s*,\s*%s|%s\s*,\s*T_LAST_ERROR\s*\(\s*\))\s*\)"
               % (ident, ident))
        if not re.search(pat, tcode):
            out.append("%s:%d: the shim names %s but the test never checks it: make the call fail and add "
                       "T_EQ(T_LAST_ERROR(), %s)" % (shim.rel, line, ident, ident))
    if (re.search(r"\bFAIL\s*\(", stripped) or re.search(r"\brt_win32_from_errno\s*\(", stripped)) \
            and not re.search(r"\bT_LAST_ERROR\s*\(\s*\)", tcode):
        out.append("%s: the shim sets the last error (FAIL( or rt_win32_from_errno( in the source) but the "
                   "test never reads T_LAST_ERROR(): check the error code of each failure return" % shim.rel)
    return out


def dynamic_case_problems(shim, report):
    out = []
    reported = {}
    for checks, failures, text in report.cases:
        reported[text] = reported.get(text, 0) + checks
    for entry in shim.cases:
        if entry not in reported:
            out.append("RT-CASES entry '%s' did not run: the test never reached T_CASE(\"%s\")" % (entry, entry))
        elif reported[entry] < 1:
            out.append("RT-CASES entry '%s' ran but made no check: add T_EQ / T_TRUE after the T_CASE" % entry)
    for text in reported:
        if text not in shim.cases:
            out.append("the test has a case the header does not list: T_CASE(\"%s\") is not an RT-CASES entry; "
                       "add it to RT-CASES or remove it" % text)
    return out


def item6_cases(target, shim, test_text, state, skip_build):
    if shim is None:
        return Result("FAIL", TITLES[6], ["not checked: the shim file could not be parsed"])
    problems, notes = [], []
    if test_text is None:
        problems.append("%s does not exist: write the test" % target.test_path)
    else:
        have = set(test_case_texts(test_text))
        for entry in shim.cases:
            if entry not in have:
                problems.append("RT-CASES entry '%s' has no T_CASE(\"%s\") in the test (the text must be "
                                "spelled exactly as in the header)" % (entry, entry))
        if shim.status in ("full", "partial"):
            problems += error_code_problems(shim, test_text)
    if shim.status in ("full", "partial"):
        doc = shim.tags["RT-DOC"]
        notes.append("note: this tool checks the failure codes the shim's source names; the reviewer compares "
                     "the failure returns listed in %s by hand" % (
                         doc if doc != "none" else "the function's documentation (RT-DOC is none)"))
    if shim.status == "trap" and not shim.cases:
        notes.append("note: a trap shim has no cases")

    if skip_build:
        if problems:
            return finish(6, problems + ["the dynamic half (the cases ran and checked something) was not run: "
                                         "--skip-build"], notes)
        return Result("SKIP", TITLES[6], ["static half passed: every RT-CASES entry has its T_CASE; the dynamic "
                                          "half (the cases ran and checked something) was not run: --skip-build"]
                      + notes)
    if blocked_reason(state):
        problems.append("the dynamic half (the cases ran and checked something) was not run: " + blocked_reason(state))
    elif state.get("report") is None:
        problems.append("not checked: the test printed no report (see item 5), so it is not known which cases ran")
    else:
        problems += dynamic_case_problems(shim, state["report"])
    return finish(6, problems, notes)


# ---- item 7: RT-TRAPS entries are reached -----------------------------------------------------

def unimplemented_calls(shim):
    """-> [(line, message or None)] for every RT_UNIMPLEMENTED(...) call in the code."""
    code = strip_comments(shim.source)
    stripped = strip_strings(code)
    out = []
    for m in re.finditer(r"\bRT_UNIMPLEMENTED\s*\(", stripped):
        line = line_of(stripped, m.start())
        got = split_call(stripped, code, m.end() - 1)
        msg = None
        if got and len(got[0]) >= 2 and re.fullmatch(r"\s*(?:%s\s*)+" % STRING_LIT, got[0][1]):
            msg = norm("".join(re.findall(r'"((?:\\.|[^"\\\n])*)"', got[0][1])))
        out.append((line, msg))
    return out


def item7_traps(shim, test_text=None):
    if shim is None:
        return Result("FAIL", TITLES[7], ["not checked: the shim file could not be parsed"])
    if shim.status not in ("partial", "trap"):
        return finish(7, [], ["note: nothing to check for status %s" % shim.status])
    problems = []
    calls = unimplemented_calls(shim)
    for line, msg in calls:
        if msg is None:
            problems.append("%s:%d: the second argument of RT_UNIMPLEMENTED must be a string literal, so that "
                            "the check can match it to RT-TRAPS and the report names the case" % (shim.rel, line))
    if shim.status == "partial":
        messages = [(line, msg) for line, msg in calls if msg is not None]
        # An entry written "core: <case>" is a stop the CORE makes (its header says "fatal"), listed
        # here so that the shim is not called full.  The shim adds no guard for it (a guard would run
        # before the core's own argument checks and change which failure the guest sees); the test
        # proves it with T_TRAPS.  All this item can check is that the test has that many T_TRAPS.
        core_stops = [e for e in shim.traps if e.lower().startswith("core:")]
        if core_stops:
            n_traps = len(re.findall(r"\bT_TRAPS\s*\(", strip_comments(test_text or "")))
            if n_traps < len(core_stops):
                problems.append("%s: %d RT-TRAPS entries are stops of the core ('core: ...') but the test has only "
                                "%d T_TRAPS: prove each one" % (shim.rel, len(core_stops), n_traps))
        for entry in shim.traps:
            if entry in core_stops:
                continue
            if not any(entry.lower() in msg.lower() for _, msg in messages):
                problems.append("%s: RT-TRAPS entry '%s' is not reached by any RT_UNIMPLEMENTED call: add "
                                "RT_UNIMPLEMENTED(ctx, \"... %s ...\") for that case, or remove the entry"
                                % (shim.rel, entry, entry))
        for line, msg in messages:
            if not any(entry.lower() in msg.lower() for entry in shim.traps):
                problems.append("%s:%d: RT_UNIMPLEMENTED(\"%s\") is not listed in RT-TRAPS: add the case to "
                                "RT-TRAPS (its text must be a substring of the message)" % (shim.rel, line, msg))
    return finish(7, problems)


# ---- item 8: the whole suite ------------------------------------------------------------------

def item8_suite(make_argv, state):
    if blocked_reason(state):
        return Result("FAIL", TITLES[8], ["not run: " + blocked_reason(state)])
    problems = test_build_problems(make_argv, state)
    if problems:
        return finish(8, ["not run: the test program does not build"] + problems)
    run = run_cmd([TEST_BINARY])
    if not run.error and run.rc == 0:
        return finish(8, [])
    lines = (run.out + run.err).split("\n")
    problems = ["the test suite did not finish: " + run.error] if run.error else \
               ["%s failed (%s)" % (TEST_BINARY, status_text(run.rc))]
    fails = [l for l in lines if l.startswith("FAIL")]
    summary = [l for l in lines if re.match(r"\d+ tests?, \d+ passed, \d+ failed", l)]
    problems += fails
    if summary:
        problems.append(summary[-1])
    if not fails and not summary:
        problems += tail(run.out + run.err, 10)
    problems.append("run 'make rt-test' for the failure messages (build/runtime/rt-test -v <lib>/<Name> for one "
                    "test); if the failing test is not yours, stop and report it in the issue")
    return finish(8, problems)


def skipped(n):
    return Result("SKIP", TITLES[n], ["skipped: --skip-build (a development aid; SKIP counts as a failure, so "
                                      "run without it before you report done)"])


# ---- main -------------------------------------------------------------------------------------

def print_result(n, res):
    print("[%d] %s  %s" % (n, res.status, res.title))
    for d in res.details:
        print("    " + d)
    sys.stdout.flush()


def main(argv=None):
    if (sys.argv[1:] if argv is None else argv) == ["--selftest"]:
        return selftest()
    ap = argparse.ArgumentParser(
        prog="check_shim.py", description="The 'done' check for one shim (docs/RUNTIME.md section 10.5).")
    ap.add_argument("shim", metavar="<lib>/<Stem>",
                    help="the shim runtime/shims/<lib>/<Stem>.c and its test runtime/tests/shims/<lib>/<Stem>.c")
    ap.add_argument("--base", default="", metavar="COMMIT", help="base commit for item 1 (default: the merge base)")
    ap.add_argument("--base-ref", default=DEFAULT_BASE_REF, metavar="REF",
                    help="branch the worktree was cut from (default %(default)s)")
    ap.add_argument("--family", default="", metavar="LIB/STEM,...",
                    help="the other shims of the family this worker was given, comma-separated; item 1 "
                         "lets their files differ from the base as well")
    ap.add_argument("--make", default="make", metavar="CMD", help="the make command (default %(default)s)")
    ap.add_argument("--skip-build", action="store_true",
                    help="development aid: skip items 4, 5, 8 and the dynamic half of 6 (counts as a failure)")
    args = ap.parse_args(argv)

    target = parse_target(args.shim)
    if target is None:
        ap.error("'%s' is not <lib>/<Stem>: write it as in 'kernel32/GetLastError' (lower-case library, "
                 "then the file name without .c)" % args.shim)
    if not os.path.isfile(TYPES_DEF):
        ap.error("%s not found: run this from the repository root (make shim-check does)" % TYPES_DEF)
    make_argv = shlex.split(args.make) or ["make"]

    shim, error = load_shim(target, TYPES_DEF, ".")
    test_text = read_text(target.test_path)
    state = {}
    results = []

    def emit(n, res):
        results.append((n, res))
        print_result(n, res)

    family = []
    for name in filter(None, (x.strip() for x in args.family.split(","))):
        sibling = parse_target(name)
        if sibling is None:
            ap.error("--family: '%s' is not <lib>/<Stem>" % name)
        family.append(sibling)
    emit(1, item1_changes(target, args.base, args.base_ref, family=family))
    emit(2, item2_names(target, shim, error, test_text, (IMPORTS_DEF, IMPORTS_EXTRA_DEF)))
    emit(3, item3_lint(target, shim, test_text))
    emit(4, skipped(4) if args.skip_build else item4_build(make_argv, state))
    emit(5, skipped(5) if args.skip_build and shim else item5_test(shim, test_text, make_argv, state))
    emit(6, item6_cases(target, shim, test_text, state, args.skip_build))
    emit(7, item7_traps(shim, test_text))
    emit(8, skipped(8) if args.skip_build else item8_suite(make_argv, state))

    bad = [n for n, r in results if r.status != "PASS"]
    name = args.shim
    if bad:
        print("shim-check %s: FAIL (items %s)" % (name, ", ".join(map(str, bad))))
        return 1
    print("shim-check %s: PASS" % name)
    return 0


def load_shim(target, types_path, root="."):
    """-> (Shim, None) or (None, message).  `root` is the directory the shim path is relative to."""
    path = os.path.join(root, target.shim_path)
    if not os.path.isfile(path):
        return None, "%s does not exist: create it (copy a finished shim as the pattern)" % target.shim_path
    try:
        com_types = os.path.join(os.path.dirname(os.path.dirname(types_path)), "com", "types.def")
        types, _ = shimdef.load_types([types_path] + ([com_types] if os.path.isfile(com_types) else []))
        return shimdef.parse_shim(path, types, root), None
    except ShimError as e:
        return None, str(e)
    except (OSError, UnicodeDecodeError) as e:
        return None, "%s: cannot read the file: %s" % (target.shim_path, e)


if __name__ == "__main__":
    sys.exit(main())
