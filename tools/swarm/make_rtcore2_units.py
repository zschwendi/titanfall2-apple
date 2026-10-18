#!/usr/bin/env python3
"""Write the briefs and unit files for the second tier of runtime core units (issue 24).

Tier 2 is the part of docs/handoff/rt-skeleton.md section 7 headed "After those": units that
sit on the tier-1 subsystems.  The brief text is the tier-1 template with a longer reading
list.  Re-running overwrites briefs and leaves existing unit files (and their state) alone.

    .venv/bin/python tools/swarm/make_rtcore2_units.py
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_rtcore_units as t1  # noqa: E402

ROOT = t1.ROOT

BRIEF = t1.BRIEF.replace(
    "  Eight other workers are building the neighbouring subsystems at the same time, each in its own\n"
    "  files.",
    "  Other workers are building the neighbouring subsystems at the same time, each in its own\n"
    "  files.").replace(
    "\n## What to build\n",
    "5. `docs/reviews/rt-core-tier1-open-questions.md` and, if it exists in your worktree,\n"
    "   `docs/reviews/rt-core-tier1-review.md`: what the authors and the reviewer of the subsystems\n"
    "   you sit on found doubtful. Do not build on a behaviour listed there as wrong; say in your\n"
    "   report where you depend on one.\n"
    "{extra_read}\n## What to build\n")
assert BRIEF != t1.BRIEF and "{extra_read}" in BRIEF

UNITS = [
    dict(
        name="wait", model="opus", sections="4.2 (and 4.1 for the object header)",
        needs=["rt-core-handle", "rt-core-time", "rt-core-lock"],
        what="waiting on kernel objects, and the event, mutex and semaphore objects",
        owned=["runtime/core/wait.c", "runtime/include/rt/sync.h (new)", "runtime/core/sync.c (new)",
               "runtime/tests/core/wait.c and sync.c (and wait_*.c, sync_*.c)",
               "runtime/tests/core/unbuilt_wait.c (delete it)"],
        allow=r"^runtime/(core/(wait|sync)\.c|include/rt/sync\.h|tests/core/(unbuilt_wait|(wait|sync)(_[a-z0-9_]+)?)\.c)$",
        must=["test -s runtime/tests/core/wait.c", "test -s runtime/tests/core/sync.c",
              "test ! -e runtime/tests/core/unbuilt_wait.c", "test -s runtime/include/rt/sync.h"],
        header_line="`runtime/include/rt/handle.h`: the wait interface you implement (`rt_dispatcher_lock`, "
                    "`rt_wait`, `rt_signal`, the `is_signaled` and `consume` operations) is already declared "
                    "there; implement it as written and report anything wrong instead of editing it. "
                    "`runtime/core/wait.c` is the placeholder you replace. `rt/sync.h` you write yourself, with a "
                    "comment on every declaration that a newcomer could implement a shim from; `rt/lock.h` and "
                    "`rt/heap.h` are models of style.",
        extra_read="6. `runtime/core/handle.c` and `runtime/core/lock.c` with their tests: how objects, names and\n"
                   "   references work, and how the lock unit tested contention with host threads.\n",
        build="""\
Two parts. **The wait core** (`core/wait.c`) as RUNTIME.md 4.2: one dispatcher lock; `rt_wait` checks
under it whether the wait can be satisfied, and if not queues the calling thread on every object
and sleeps on that thread's own wake primitive; `rt_signal`, called by a kind under the lock after
it changed an object's state, satisfies queued waits in queue order and consumes on the waiter's
behalf, re-checking the object after each consume. Wait-any returns the lowest satisfied index;
wait-all observes and consumes every object atomically, so two threads waiting for the same two
mutexes in opposite orders cannot deadlock and neither holds one while waiting for the other.
Timeouts run on the runtime's monotonic clock (`rt/time.h`, which can be replaced by a test clock:
say how a sleeping wait behaves when it is). `timeout 0` polls and never sleeps. Windows argument
rules to implement and test, each stated in a comment: `n` of 0 or above 64, the same object twice
in a wait-all, an object whose kind is not waitable, `WAIT_ABANDONED + i` and what it does to the
mutex, which index is reported when several objects are signalled. Alertable waits: a per-thread
queue of asynchronous procedure calls (`rt_apc_queue`, for a later `QueueUserAPC` shim), run through
`rt_guest_call` with the dispatcher lock released, after which the wait returns
`WAIT_IO_COMPLETION`; a non-alertable wait never runs one. Per-thread state lives in your slot
`RT_SUB_WAIT` of `RtThread.sub[]`; create it on a thread's first wait and give the future thread
unit one function to call at thread exit that frees it and abandons the mutexes that thread owns.
**The three kinds** (`rt/sync.h`, `core/sync.c`), as handle-table objects with the operations the
shims `CreateEventA/W`, `OpenEventA/W`, `SetEvent`, `ResetEvent`, `PulseEvent`, `CreateMutexA/W`,
`OpenMutexW`, `ReleaseMutex`, `CreateSemaphoreA/W`, `ReleaseSemaphore`, `WaitForSingleObject(Ex)`,
`WaitForMultipleObjects(Ex)`, `SignalObjectAndWait` and `SleepEx` will call: manual-reset and
auto-reset events (a Set on an auto-reset event with waiters releases exactly one and leaves it
reset; with none it stays set until one wait consumes it); recursive mutexes owned by a thread
(release by a non-owner fails with `ERROR_NOT_OWNER`, initial ownership, recursion depth,
abandonment); semaphores with an initial and a maximum count (a release that would pass the
maximum fails with `ERROR_TOO_MANY_POSTS` and changes nothing; the previous count is reported).
Named objects go through the handle unit's name space: creating a name that exists returns the
existing object and sets `ERROR_ALREADY_EXISTS`, and a name held by another kind fails with
`ERROR_INVALID_HANDLE`. `PulseEvent`: implement it by the documented rule or trap, and say which.
The thread and process kinds (signalled on exit) belong to the thread unit: make sure your
interface lets that unit define them without touching your files. The runtime cannot create guest
threads yet: test with host `pthread`s that each own a hand-made `Ctx` and thread block, as
`runtime/tests/core/lock_stress.c` and `handle_threads.c` do. Tests must include contention and
ordering, not only single-thread state checks: exactly-one wake-up per auto-reset Set under many
waiters, every waiter released by a manual-reset Set, a semaphore used as a counted queue with no
lost or duplicated permit, wait-all atomicity, a wait that times out while a signal arrives, and
an abandoned mutex. For the dispatcher lock itself, the virtual-memory unit measured a `pthread`
mutex taking over 120 s where an `os_unfair_lock` took under 2 s for 160,000 calls from 8 threads
on this machine: choose with a measurement of your own and state it. Correct under arm64's weak
memory ordering; the x86-ordering mode is not available in test processes.""",
    ),
    dict(
        name="registry", model="sonnet", sections="8.3 (and 4.1 for handle objects)",
        needs=["rt-core-handle", "rt-core-path", "rt-core-locale", "rt-core-machine"],
        what="the registry: an in-memory tree kept in a text file",
        owned=["runtime/include/rt/registry.h (new)", "runtime/core/registry.c (new)",
               "runtime/tests/core/registry.c (and registry_*.c)"],
        allow=r"^runtime/(core/registry\.c|include/rt/registry\.h|tests/core/registry(_[a-z0-9_]+)?\.c)$",
        must=["test -s runtime/tests/core/registry.c", "test -s runtime/include/rt/registry.h"],
        header_line="The neighbouring headers `runtime/include/rt/env.h`, `rt/locale.h` and `rt/handle.h` as models "
                    "of style: you write `rt/registry.h` yourself, with a comment on every declaration that a "
                    "newcomer could implement a shim from.",
        extra_read="6. `docs/findings/30-proton-trace.md`, the registry paragraphs: what the real game asked for in\n"
                   "   one traced session (very little). And `docs/findings/24-import-surface.md`, the `Reg*` rows.\n",
        build="""\
There is no header yet: create `runtime/include/rt/registry.h`, `runtime/core/registry.c` and define
`rt_registry_init` (a weak no-op in `core/init.c` today; yours replaces it by being linked). As
RUNTIME.md 8.3: an in-memory tree of keys and values, loaded at start-up from one text file in the
state directory (`rt_config()->state_dir`) and seeded, when a key is absent from the file, with the
handful every Windows machine has: the processor description and the operating-system version,
whose contents must come from `rt/machine.h` so that they cannot disagree with `GetVersionEx` and
`cpuid`. Design the interface for the shims that will sit on it: `RegOpenKeyExA/W`,
`RegCreateKeyExA/W` (with the disposition), `RegCloseKey`, `RegQueryValueExA/W`,
`RegSetValueExA/W`, `RegDeleteValueA`, `RegEnumValueW`, `RegEnumKeyExA/W`, `RegQueryInfoKeyW`. Open
keys are handle-table objects; the predefined roots are the Windows constants (`HKEY_CURRENT_USER`
is `0x80000001` sign-extended to 64 bits, and a guest may pass it truncated or extended: state the
rule), `HKEY_CLASSES_ROOT` is a view of `HKEY_LOCAL_MACHINE\\Software\\Classes`, and the 32-bit and
64-bit view flags are accepted and name the same tree. Windows rules to get right and test, each
stated in a comment: key and value names compare case-insensitively and keep the case they were
created with; the default value is the NULL or empty name; a query with a NULL buffer returns the
size, one with a small buffer returns `ERROR_MORE_DATA` and the size; string data is stored as
written, with or without its terminator, and returned as stored; the `A` calls convert names and
string data through code page 1252 with `rt/locale.h` and report sizes in bytes of the converted
text; `REG_SZ`, `REG_EXPAND_SZ`, `REG_MULTI_SZ`, `REG_DWORD`, `REG_QWORD` and `REG_BINARY` are kept
exactly; enumeration indices are stable while nothing changes. A key or value that is not there
returns `ERROR_FILE_NOT_FOUND` and is **logged once per name** (first runs tell us what the game
asks for; each later addition to the seed is deliberate). Never invent a value for an unknown key.
Do not seed anything under the keys of the game's publisher or its launcher: what the game sees
there is a decision for the project's owner, and absent is the correct default. Writes persist:
the file is rewritten whole to a temporary name and renamed into place (this house loses power
without warning; a half-written file must never replace a good one), in a stable order so that two
saves of the same tree are byte-identical; say when you save (each change, or on close and flush)
and why. The file format is yours: plain text a person can read and edit, documented at the top of
the header, able to hold every value type and any name (define the escaping), and a file that does
not parse stops the runtime with the line number instead of being skipped. Thread-safe.""",
    ),
]


def main():
    for u in UNITS:
        n = u["name"]
        uid = "rt-core-" + n
        cmds = ["test -z \"$(git diff --name-only $(git merge-base HEAD wip/lifter-v0) HEAD | grep -v -E '%s')\""
                % u["allow"]] + u["must"] + ["make rt", "make rt-test"]
        placeholder_rule = ("- Delete `runtime/tests/core/unbuilt_%s.c`: it asserts that your functions are unbuilt. "
                            "No function of your interface may still end in the unbuilt-function report unless the "
                            "design says that case traps, and then say which in your report.\n" % n
                            ) if any("unbuilt_" in f for f in u["owned"]) else ""
        brief = BRIEF.format(name=n, what=u["what"], sections=u["sections"], build=u["build"],
                             owned_list="\n".join("  - `%s`" % f for f in u["owned"]),
                             header_line=u["header_line"], extra_read=u["extra_read"],
                             placeholder_rule=placeholder_rule, check_block="\n".join("    " + c for c in cmds))
        with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
            f.write(brief)
        path = os.path.join(ROOT, "swarm", "units", uid + ".json")
        if os.path.exists(path):
            continue
        unit = {"id": uid, "title": "Runtime core: " + u["what"], "kind": "runtime-core", "issue": 24,
                "model": u["model"], "priority": 14, "needs": u["needs"], "check": cmds, "state": "todo"}
        with open(path, "w") as f:
            json.dump(unit, f, indent=2)
            f.write("\n")
        print("wrote", uid, u["model"])


if __name__ == "__main__":
    main()
