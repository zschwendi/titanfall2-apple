#!/usr/bin/env python3
"""Write the briefs and unit files for families of Windows API shims (issue 24).

One unit is one family: a handful of imports that sit on the same runtime core subsystem and
that the C runtime uses together.  One worker writes the two files of each member and proves
each with `make shim-check`.  Re-running overwrites briefs and leaves existing unit files (and
their state) alone.

    .venv/bin/python tools/swarm/make_shim_units.py

Add a family to FAMILIES when the core it needs is merged and every type and constant its
signatures need is in runtime/tools/types.def and runtime/include/rt/win/ (the planner adds
those first: a worker may not).
"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FAMILIES = [
    dict(
        id="critsec", lib="kernel32", core="rt/lock.h", sections="6.4", needs=["rt-core-lock"],
        what="critical sections",
        members=["InitializeCriticalSection", "InitializeCriticalSectionAndSpinCount", "InitializeCriticalSectionEx",
                 "DeleteCriticalSection", "EnterCriticalSection", "LeaveCriticalSection", "TryEnterCriticalSection"],
        notes="""\
- These are the engine's hottest calls (3,371 call sites). The shim must add nothing to the core
  call but the argument checks Windows documents: no logging, no allocation, on the normal path.
- The spin count is advisory (the core does its own waiting): accept any value. Say in a comment
  what the high bit of the spin count meant on old Windows and that it is ignored.
- `InitializeCriticalSectionEx` takes flags: `CRITICAL_SECTION_NO_DEBUG_INFO` is in
  `rt/win/sync.h`. Decide from the documentation what an unknown flag does; if it does not say,
  that case is `RT_UNIMPLEMENTED`.
- What the core does with a section that was never initialised, or a leave by a thread that is
  not the owner, is described in `rt/lock.h`: test what the header promises, do not add checks.""",
    ),
    dict(
        id="srw", lib="kernel32", core="rt/lock.h", sections="6.4", needs=["rt-core-lock"],
        what="slim reader/writer locks and condition variables",
        members=["InitializeSRWLock", "AcquireSRWLockExclusive", "AcquireSRWLockShared", "ReleaseSRWLockExclusive",
                 "ReleaseSRWLockShared", "InitializeConditionVariable", "SleepConditionVariableSRW",
                 "WakeConditionVariable", "WakeAllConditionVariable"],
        notes="""\
- `SleepConditionVariableSRW` returns FALSE with `ERROR_TIMEOUT` when the time runs out, and its
  `Flags` argument may hold `CONDITION_VARIABLE_LOCKMODE_SHARED` (in `rt/win/sync.h`). Test the
  timeout path with a short timeout (10 ms) and both lock modes; the lock must be held again
  when the call returns, in the mode it was held before.
- All-zero is a valid, unlocked lock and a valid condition variable: test that the initialisers
  produce exactly that.""",
    ),
    dict(
        id="slist", lib="kernel32", core="rt/lock.h", sections="6.4", needs=["rt-core-lock"],
        what="interlocked singly linked lists",
        members=["InitializeSListHead", "InterlockedPushEntrySList", "InterlockedPopEntrySList",
                 "InterlockedFlushSList", "QueryDepthSList"],
        notes="""\
- Return values are what the guest uses: push returns the previous first entry (NULL for an empty
  list), pop returns the removed entry or NULL, flush returns the whole chain and leaves the list
  empty, depth is a 16-bit count.
- The header is 16 bytes and must be 16-byte aligned; entries too. Allocate test storage with
  that alignment. Inlined guest code reads the depth from the low 16 bits of the first 8 bytes:
  check that field directly in the test after pushes and pops, as well as through the call.""",
    ),
    dict(
        id="heap", lib="kernel32", core="rt/heap.h", sections="5.4", needs=["rt-core-heap"],
        what="the process heap and created heaps",
        members=["GetProcessHeap", "HeapAlloc", "HeapFree", "HeapReAlloc", "HeapSize", "HeapCreate", "HeapDestroy"],
        notes="""\
- The documentation says which of these set the last error and which do not (`HeapAlloc` does
  not call `SetLastError` on failure). Follow it exactly and test it: plant a known last error
  before the call and check it afterwards.
- `HeapSize` reports failure as `(SIZE_T)-1`. `HeapFree` of NULL: see the documentation.
- Flags: the ones the core knows are in `rt/heap.h`, with what it does for each. A flag bit
  that is neither in that header nor documented for the function is `RT_UNIMPLEMENTED`, never
  ignored.
- What the core does with a pointer from another heap or a corrupted block is a fatal report
  (see `rt/heap.h`): cover one such case per function with `T_TRAPS`.""",
    ),
    dict(
        id="vm", lib="kernel32", core="rt/vm.h", sections="5.1 and 5.2", needs=["rt-core-vm"],
        what="virtual memory",
        members=["VirtualAlloc", "VirtualFree", "VirtualQuery"],
        notes="""\
- The Windows argument rules live in the core (`runtime/core/vm.c` states each one in a comment
  where it is implemented; read them). The shim passes arguments through and translates results.
- **`VirtualFree` must refuse memory that is not private.** The core's free call works on every
  kind of region (images and file views included, because the loader uses it too). Ask
  `rt_vm_query` first; if the region is an image or a mapped view, fail the way the
  documentation says, and if it does not say, that case is `RT_UNIMPLEMENTED`.
- `VirtualQuery` returns the number of bytes written and 0 on failure; a buffer shorter than the
  structure fails. The structure is 48 bytes (`rt/win/memory.h`).
- The guest's page size is 4 KB and its allocation granularity 64 KB, whatever the host's are:
  test addresses and sizes that are multiples of 4 KB but not of 16 KB.""",
    ),
    dict(
        id="clock", lib="kernel32", core="rt/time.h", sections="8.1", needs=["rt-core-time"],
        what="clocks",
        members=["QueryPerformanceCounter", "QueryPerformanceFrequency", "GetSystemTimeAsFileTime",
                 "GetTickCount", "GetTickCount64"],
        notes="""\
- Every function is arithmetic on one of the core's two clocks; do no arithmetic of your own
  that the core already offers. `GetTickCount` is the low 32 bits of the 64-bit tick count.
- The core lets a test replace the clock with a fixed one (see `rt/time.h`): use it, so that
  the tests check exact values and not "roughly now". Also check, with the real clock, that two
  calls in a row never go backwards.""",
    ),
    dict(
        id="machine", lib="kernel32", core="rt/machine.h", sections="8 (intro) and 8.5",
        needs=["rt-core-machine", "rt-core-locale"],
        what="the machine description, and wide debug output",
        members=["IsProcessorFeaturePresent", "GetSystemInfo", "GetVersion", "OutputDebugStringW"],
        notes="""\
- The answers come from `rt/machine.h` and nowhere else; a shim holds no number of its own.
  `GetVersion` packs major, minor and build into one DWORD as the documentation describes: test
  the exact value for Windows 7 SP1 (6.1, build 7601).
- `IsProcessorFeaturePresent` takes the `PF_*` numbers of the Windows headers; there are no
  names for them in our headers, so write the numbers in the test with a comment naming each.
- `OutputDebugStringW` is the wide twin of the existing `kernel32/OutputDebugStringA`: same log
  channel, same line splitting. Convert the text with `rt/locale.h` (never host `wchar_t`), and
  state what happens to characters outside ASCII.""",
    ),
    dict(
        id="handle", lib="kernel32", core="rt/handle.h", sections="4.1", needs=["rt-core-handle"],
        what="handles",
        members=["GetCurrentProcess", "GetCurrentThread", "CloseHandle", "DuplicateHandle"],
        notes="""\
- The two pseudo-handles are constants in `rt/win/base.h`.
- `CloseHandle` goes through the core's close call, which already knows the kinds that must be
  closed another way and the stale-handle rule. Tests need an object to close: make one with the
  core's own calls (see `runtime/tests/core/handle.c` for how).
- `DuplicateHandle`: both process arguments must name this process (the pseudo-handle);
  anything else is `RT_UNIMPLEMENTED`. `DUPLICATE_CLOSE_SOURCE` and `DUPLICATE_SAME_ACCESS` are
  in `rt/win/base.h`. Access rights are not modelled: say so in a comment, and say what the
  shim does with the access argument. A NULL target pointer with `DUPLICATE_CLOSE_SOURCE` is a
  documented way to close a handle.""",
    ),
]

BRIEF = """# Unit shim-{id}: Windows API shims for {what}

This project statically recompiles a 64-bit Windows game to native arm64 macOS: the game's code
becomes C, and Windows becomes a runtime we write (`runtime/`). When the game calls an imported
Windows function, the call lands in a *shim*: a small host C function that checks the arguments
as Windows documents, calls the runtime's core, and hands back the result. The core for your
family is built and tested. Your job is the shims for {n} functions of `{lib}.dll`:

{member_list}

## Where you work

- Your worktree is the directory you were given (`.wt/shim-{id}`), branch `swarm/shim-{id}`.
  Work only inside it. Do not push, merge or switch branches.
- **You create exactly two files per function and edit nothing else:**
  `runtime/shims/{lib}/<Name>.c` and `runtime/tests/shims/{lib}/<Name>.c`.
- **Commit after each function passes its check** (the account's usage limit can stop you
  without warning, and only committed work survives). One commit per function is right.
- If something you need is missing (a type that is not in `runtime/tools/types.def`, a constant
  or struct that is not under `runtime/include/rt/win/`, a core function that does not exist),
  do not add it and do not work around it: skip that function, finish the others, and say
  exactly what was missing in your report.
- Python is `.venv/bin/python`. Never read anything under `game/`. Install nothing. Never use
  `pkill` or `killall`.
- You may read the Microsoft documentation page of each function on `learn.microsoft.com` if
  you have a tool that fetches web pages. Nothing else from the network.

## Read first

1. `runtime/README.md`, all of it: it is the contract for a shim and its test.
2. `docs/RUNTIME.md` sections 3 (shim rules), 9.2 (what a shim may do when it does not handle
   a case) and {sections} (your subsystem).
3. `runtime/include/{core}`: the core interface your shims call. The comments there are the
   specification; the behaviour is already implemented and tested in `runtime/core/` and
   `runtime/tests/core/`. Other headers you may include are listed in the README.
4. Two finished shims with their tests, as models: `runtime/shims/kernel32/EncodePointer.c` and
   `OutputDebugStringA.c`, and the same names under `runtime/tests/shims/kernel32/`. And
   `runtime/include/rt/test.h` for the test macros.

## Rules

- **The Microsoft documentation of each function is the specification**: its parameters, its
  return value, when it sets the last error and to what. The `SHIM` line copies the documented
  prototype, with our type names.
- **Never guess.** If you are not sure what Windows does in some case, do not pick something
  plausible: make that case `RT_UNIMPLEMENTED` (status `partial`, listed in `RT-TRAPS`) and
  name it in your report. A loud stop is cheap to fix later; a quiet wrong answer is not.
- A shim is thin. Logic that belongs to the subsystem is already in the core: call it. Do not
  re-implement it, do not call another shim, do not call `malloc`.
- Tests check what the guest can observe: return values, the last error (also that it is left
  alone on success), and every field the function writes. Do not start threads in a shim test
  (the test macros are not thread-safe); contention is covered by the core's own tests.
- Other workers are building on this machine at the same time. If a test that is not yours
  fails, run it once more; if it fails again, leave it alone and report it.

Notes for this family:

{notes}

## Done

On a clean, committed worktree every one of these exits 0:

{check_block}

Run each as you finish that function, not only at the end. `make shim-check` prints which of
its eight items failed and why; fix the shim or the test, never the checker.

## Report

Reply in under 40 lines: which functions pass `make shim-check`; any you skipped and exactly
what was missing; every case you turned into `RT_UNIMPLEMENTED` and why; every Windows
behaviour you were unsure of; anything wrong you noticed in the core or in shared files. An
honest partial result is useful. A result claimed complete that fails its check is not.
"""


def main():
    for f in FAMILIES:
        uid = "shim-" + f["id"]
        names = ["%s/%s" % (f["lib"], m) for m in f["members"]]
        cmds = []
        for n in names:
            others = ",".join(x for x in names if x != n)
            cmds.append("make shim-check SHIM=%s SHIM_FAMILY=%s" % (n, others))
        brief = BRIEF.format(id=f["id"], what=f["what"], n=len(names), lib=f["lib"], core=f["core"],
                             sections=f["sections"], notes=f["notes"],
                             member_list="\n".join("- `%s`" % m for m in f["members"]),
                             check_block="\n".join("    " + c for c in cmds))
        with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as out:
            out.write(brief)
        path = os.path.join(ROOT, "swarm", "units", uid + ".json")
        if os.path.exists(path):
            continue
        unit = {"id": uid, "title": "Shims: %s (%d functions of %s)" % (f["what"], len(names), f["lib"]),
                "kind": "shim-family", "issue": 24, "model": "haiku", "priority": 20,
                "needs": f["needs"], "check": cmds, "state": "todo"}
        with open(path, "w") as out:
            json.dump(unit, out, indent=2)
            out.write("\n")
        print("wrote", uid, len(names))


if __name__ == "__main__":
    main()
