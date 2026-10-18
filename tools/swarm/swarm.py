#!/usr/bin/env python3
"""Ledger for parallel worker runs.  The state lives on disk, not in any chat session.

A *unit* is one piece of work one worker can finish alone: an instruction family, a group of
Windows API shims.  Each unit is a JSON file in swarm/units/, has a brief in swarm/briefs/, and
is worked on in its own git worktree (.wt/<id>) on its own branch (swarm/<id>).  Only the
orchestrator runs this tool; workers never edit swarm/.

    todo --claim--> claimed --check passes--> passed --merge--> merged
                       |
                       +--reset--> todo        (interrupted or failed: redone from scratch)

After a usage limit, a crash or a reboot:   tools/swarm/swarm.py resume
It re-checks every claimed unit.  One whose check passes becomes passed; one with unfinished
work in its worktree is listed as interrupted, to be continued; one with no work is reset.

    swarm.py status                 table of all units
    swarm.py next [N]               the next N todo units, in priority order
    swarm.py claim ID [ID...]       create branch and worktree, mark claimed
    swarm.py check ID [ID...]       run the unit's checks in its worktree; pass marks it passed
    swarm.py reset ID [ID...]       delete worktree and branch, back to todo
    swarm.py merge ID [ID...]       merge a passed unit into the current branch of the main checkout
    swarm.py resume [--reset]       reconcile after an interruption (--reset discards unfinished work)
    swarm.py report                 markdown table for a GitHub issue
"""

import contextlib
import datetime
import fcntl
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UNITS = os.path.join(ROOT, "swarm", "units")
LOGS = os.path.join(ROOT, "swarm", "logs")
WT = os.path.join(ROOT, ".wt")

# Ignored inputs every worktree needs.  lifted/ and harness/out/ are deliberately not shared:
# they depend on the worker's own lifter.
SHARED = ["game", ".venv", "inventory", "tests/corpus/build", "third_party"]
CHECK_TIMEOUT = 3600


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def git(*args, cwd=ROOT, check=True):
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if check and r.returncode:
        sys.exit("git %s failed in %s:\n%s" % (" ".join(args), cwd, r.stderr.strip()))
    return r


def load(uid):
    path = os.path.join(UNITS, uid + ".json")
    if not os.path.exists(path):
        sys.exit("no such unit: %s" % uid)
    with open(path) as f:
        return json.load(f)


def save(u):
    path = os.path.join(UNITS, u["id"] + ".json")
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(u, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def all_units():
    if not os.path.isdir(UNITS):
        return []
    us = [load(n[:-5]) for n in sorted(os.listdir(UNITS)) if n.endswith(".json")]
    return sorted(us, key=lambda u: (u.get("priority", 50), u["id"]))


def note(u, event, **extra):
    u.setdefault("history", []).append(dict(at=now(), event=event, **extra))


def branch(u):
    return "swarm/" + u["id"]


def worktree(u):
    return os.path.join(WT, u["id"])


def branch_exists(u):
    return git("rev-parse", "--verify", "--quiet", "refs/heads/" + branch(u), check=False).returncode == 0


def exclude_shared():
    """The .gitignore patterns end in '/', which does not match a symlink.  Ignore the links locally."""
    path = os.path.join(ROOT, git("rev-parse", "--git-common-dir").stdout.strip(), "info", "exclude")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    have = open(path).read().split("\n") if os.path.exists(path) else []
    missing = ["/" + rel for rel in SHARED if "/" + rel not in have]
    if missing:
        with open(path, "a") as f:
            f.write("\n".join(missing) + "\n")


PAUSE_FILE = os.path.join(ROOT, "swarm", "logs", "PAUSE")


def claim(uid):
    # A way to drain running waves: while swarm/logs/PAUSE exists nothing new is claimed, so a
    # wave lets its workers in flight finish, marks the rest not-claimed and ends.
    if os.path.exists(PAUSE_FILE):
        print("%s: not claimed: the ledger is paused (remove swarm/logs/PAUSE to go on)" % uid)
        return
    exclude_shared()
    u = load(uid)
    if u["state"] != "todo":
        print("%s: is %s, not todo; skipped" % (uid, u["state"]))
        return
    waiting = [n for n in u.get("needs", []) if load(n)["state"] != "merged"]
    if waiting:
        print("%s: not ready, needs %s merged first; skipped" % (uid, ", ".join(waiting)))
        return
    wt = worktree(u)
    if os.path.exists(wt) or branch_exists(u):
        sys.exit("%s: stale worktree or branch; run 'reset %s' first" % (uid, uid))
    base = git("rev-parse", u.get("base", "HEAD")).stdout.strip()
    os.makedirs(WT, exist_ok=True)
    git("worktree", "add", "-q", "-b", branch(u), wt, base)
    for rel in SHARED:
        if rel in u.get("unshare", []):       # the unit builds its own copy of this directory
            continue
        src, dst = os.path.join(ROOT, rel), os.path.join(wt, rel)
        if os.path.exists(src) and not os.path.lexists(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.symlink(src, dst)
    u.update(state="claimed", base_commit=base, attempts=u.get("attempts", 0) + 1)
    note(u, "claimed", base=base[:12])
    save(u)
    print("%s: claimed, worktree %s" % (uid, wt))


def run_checks(u):
    """(passed, reason).  Passing needs a clean tree, a commit beyond the base, and every check exiting 0."""
    wt = worktree(u)
    if not os.path.isdir(wt):
        return False, "no worktree"
    if git("status", "--porcelain", cwd=wt).stdout.strip():
        return False, "uncommitted changes in the worktree"
    ahead = int(git("rev-list", "--count", "%s..HEAD" % u["base_commit"], cwd=wt).stdout or 0)
    if not ahead:
        return False, "no commits beyond the base"
    # The checks run on the unit's work combined with the integration branch as it is now, which
    # is what a merge will produce: a unit that branched from a base with a fault that has since
    # been fixed is not failed for it, and one that no longer fits the branch is found out here.
    into = git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    if git("merge", "--no-edit", "-q", into, cwd=wt, check=False).returncode != 0:
        git("merge", "--abort", cwd=wt, check=False)
        return False, "%s does not merge into the unit's branch without conflicts" % into
    os.makedirs(LOGS, exist_ok=True)
    log = os.path.join(LOGS, "%s.%d.log" % (u["id"], u.get("attempts", 1)))
    with open(log, "w") as f:
        for cmd in u["check"]:
            f.write("\n$ %s\n" % cmd)
            f.flush()
            try:
                rc = subprocess.run(cmd, shell=True, cwd=wt, stdout=f, stderr=subprocess.STDOUT,
                                    timeout=u.get("check_timeout", CHECK_TIMEOUT)).returncode
            except subprocess.TimeoutExpired:
                rc = "timeout"
            f.write("[exit %s]\n" % rc)
            if rc != 0:
                return False, "check failed (%s): %s    log: %s" % (rc, cmd, log)
    return True, log


def check(uid):
    u = load(uid)
    if u["state"] not in ("claimed", "passed"):
        print("%s: is %s; nothing to check" % (uid, u["state"]))
        return False
    ok, why = run_checks(u)
    if ok:
        head = git("rev-parse", "HEAD", cwd=worktree(u)).stdout.strip()
        u.update(state="passed", passed_commit=head)
        note(u, "passed", commit=head[:12])
        print("%s: PASSED at %s" % (uid, head[:12]))
    else:
        if u["state"] == "passed":
            u["state"] = "claimed"
        note(u, "check-failed", why=why)
        print("%s: not passed: %s" % (uid, why))
    save(u)
    return ok


def reset(uid):
    u = load(uid)
    if u["state"] == "merged":
        sys.exit("%s: already merged; not resetting" % uid)
    wt = worktree(u)
    if os.path.isdir(wt):
        git("worktree", "remove", "--force", wt, check=False)
        shutil.rmtree(wt, ignore_errors=True)
    git("worktree", "prune")
    if branch_exists(u):
        git("branch", "-D", branch(u))
    u["state"] = "todo"
    u.pop("passed_commit", None)
    note(u, "reset")
    save(u)
    print("%s: reset to todo" % uid)


def merge(uid):
    u = load(uid)
    if u["state"] != "passed":
        print("%s: is %s, not passed; skipped" % (uid, u["state"]))
        return
    # The ledger's own files change as units move; anything else uncommitted blocks a merge.
    dirty = [l for l in git("status", "--porcelain", "--untracked-files=no").stdout.splitlines()
             if not l[3:].startswith("swarm/")]
    if dirty:
        sys.exit("main checkout has uncommitted changes; commit them before merging:\n" + "\n".join(dirty))
    r = git("merge", "--no-ff", "-m", "Merge %s: %s" % (branch(u), u["title"]), u["passed_commit"], check=False)
    if r.returncode:
        git("merge", "--abort", check=False)
        note(u, "merge-conflict")
        save(u)
        print("%s: MERGE CONFLICT, aborted. Resolve by hand, or reset and redo on the new base.\n%s"
              % (uid, r.stdout.strip()))
        return
    u["state"] = "merged"
    note(u, "merged", into=git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip())
    save(u)
    git("worktree", "remove", "--force", worktree(u), check=False)
    git("branch", "-D", branch(u), check=False)
    print("%s: merged" % uid)


def progress(u):
    """(commits beyond the base, True if the worktree has uncommitted changes), or None without a worktree."""
    wt = worktree(u)
    if not os.path.isdir(wt):
        return None
    ahead = int(git("rev-list", "--count", "%s..HEAD" % u["base_commit"], cwd=wt).stdout or 0)
    return ahead, bool(git("status", "--porcelain", cwd=wt).stdout.strip())


def resume(reset_unfinished=False):
    """After an interruption.  A claimed unit whose check passes becomes passed.  One that does
    not pass but has work in its worktree is left claimed and listed as interrupted, so a new
    agent (or the same one, resumed) can continue from its commits; --reset throws that work
    away instead.  A claimed unit with nothing in its worktree is reset either way."""
    interrupted = []
    for u in all_units():
        if u["state"] != "claimed":
            continue
        if check(u["id"]):
            continue
        p = progress(u)
        if reset_unfinished or p is None or p == (0, False):
            reset(u["id"])
        else:
            interrupted.append((u, p))
    status()
    for u, (ahead, dirty) in interrupted:
        print("INTERRUPTED %-24s %d commit(s)%s in %s: continue it (see swarm/README.md) or 'reset' it"
              % (u["id"], ahead, ", uncommitted changes" if dirty else "", worktree(u)))


def status():
    us = all_units()
    count = {}
    for u in us:
        count[u["state"]] = count.get(u["state"], 0) + 1
        print("%-8s %-30s %-7s try %d  #%-4s %s" % (u["state"], u["id"], u.get("model", "-"), u.get("attempts", 0),
                                                 u.get("issue", "-"), u["title"]))
    print("\n" + ", ".join("%d %s" % (count[s], s) for s in ("todo", "claimed", "passed", "merged") if s in count))


def report():
    print("| Unit | State | Model | Attempts | Issue | Commit |\n|---|---|---|---|---|---|")
    for u in all_units():
        print("| %s | %s | %s | %d | %s | %s |" % (u["title"], u["state"], u.get("model", ""), u.get("attempts", 0),
                                               "#%s" % u["issue"] if u.get("issue") else "",
                                               u.get("passed_commit", "")[:12]))


@contextlib.contextmanager
def repo_lock():
    """One claim, reset or merge at a time across every process that uses this ledger: two waves
    (tools/swarm/wave.py) and the orchestrator may run side by side, and each of these commands
    changes the main checkout or the list of worktrees.  Waits for the holder."""
    with open(os.path.join(ROOT, ".git", "swarm.lock"), "w") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(f, fcntl.LOCK_UN)


def main():
    cmd, args = (sys.argv[1], sys.argv[2:]) if len(sys.argv) > 1 else ("status", [])
    each = {"claim": claim, "check": check, "reset": reset, "merge": merge}
    if cmd in each:
        if not args:
            sys.exit("usage: swarm.py %s ID [ID...]" % cmd)
        for uid in args:
            if cmd == "check":                 # long, and only in the unit's own worktree
                check(uid)
            else:
                with repo_lock():
                    each[cmd](uid)
    elif cmd == "next":
        merged = {u["id"] for u in all_units() if u["state"] == "merged"}
        ready = [u for u in all_units() if u["state"] == "todo" and set(u.get("needs", [])) <= merged]
        for u in ready[:int(args[0]) if args else 8]:
            print("%-30s %-7s %s" % (u["id"], u.get("model", "-"), u["title"]))
    elif cmd == "resume":
        resume(reset_unfinished="--reset" in args)
    elif cmd == "report":
        report()
    elif cmd == "status":
        status()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
