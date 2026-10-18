#!/usr/bin/env python3
"""Write the briefs and unit files for the first tier of runtime core units (issue 24).

One unit owns one subsystem of the native runtime: its source file, its tests, and (for the two
with no header yet) its header.  The cut is the one proposed in docs/handoff/rt-skeleton.md
section 7.  Re-running overwrites briefs and leaves existing unit files (and their state) alone.

    .venv/bin/python tools/swarm/make_rtcore_units.py
"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

UNITS = [
    dict(
        name="vm", model="opus", sections="5.1, 5.2 and 5.3", placeholder=True, header=True,
        what="the virtual-memory map behind `VirtualAlloc`, `VirtualFree`, `VirtualProtect` and `VirtualQuery`",
        build="""\
`runtime/core/vm.c` today is a simple stand-in. Replace it with the real map of RUNTIME.md 5.2:
regions (one per reservation, base 64 KB aligned, first fit from the VM arena or at the requested
address when that range is free inside the arena), a state (free, reserved, committed) and a
protection for every 4 KB guest page kept in a side array, and host pages (16 KB here; ask the
system, never hard-code) accessible with the **union** of the protections of the committed guest
pages they contain. Commit yields zeros; decommitting part of a host page zeroes those 4 KB pages
at once so a later commit sees zeros; decommitting a whole host page drops it. `rt_vm_protect`
returns the exact previous protection of the first page. `rt_vm_query` answers at 4 KB precision
with `MEM_IMAGE` and the module base for images, `MEM_MAPPED` for file views, `MEM_PRIVATE`
otherwise, and for addresses outside both arenas asks the kernel and reports committed private
memory. Execute permissions are recorded and treated as read. `PAGE_GUARD` and `PAGE_NOACCESS` are
exact only when they cover whole host pages, otherwise recorded and logged once. `MEM_LARGE_PAGES`
fails with `ERROR_PRIVILEGE_NOT_HELD`; `MEM_RESET` succeeds and does nothing. Every Windows rule
for the argument combinations matters (reserve then commit in pieces, commit without a prior
reserve, `MEM_RELEASE` needing the region base and size 0, `MEM_DECOMMIT` on a sub-range, freeing
across regions failing, zero size, unaligned addresses rounding to pages): state each rule in a
comment where you implement it and test it. The map is used from several threads: one lock, held
briefly, never across a guest call. The existing `runtime/tests/core/vm.c` cases must keep passing
except where they assert the stand-in's limits; extend that file.""",
    ),
    dict(
        name="lock", model="opus", sections="6.4", placeholder=True, header=True,
        what="the four lock structures the game embeds in its own objects: critical sections, SRW locks, condition variables and interlocked singly linked lists",
        build="""\
Implement `rt/lock.h` exactly as RUNTIME.md 6.4 lays the structures out: `CRITICAL_SECTION` (40
bytes, the listed field offsets, recursive, `OwningThread` and `RecursionCount` kept truthful,
must be initialised, `Delete` zeroes it), `SRWLOCK` (8 bytes, all-zero is a valid unlocked lock),
`CONDITION_VARIABLE` (8 bytes, all-zero valid, `SleepConditionVariableSRW` releases, waits on the
wake sequence, re-acquires, with a timeout), `SLIST_HEADER` (16 bytes, 16-aligned, depth in bits
0-15 of the low qword where inlined `QueryDepthSList` reads it, push, pop and flush as one 128-bit
compare-and-swap with a sequence number against ABA). All of them block on one primitive,
`os_sync_wait_on_address` and its wake calls (`<os/os_sync_wait_on_address.h>`, macOS 14.4+),
applied to a word inside the guest structure. None allocates and none uses the handle table. The
uncontended path of each must be a single atomic operation. These are the engine's hottest locks
(3,371 critical-section sites) and a bug here is a silent race, so reason about each state
transition in a comment, including lost-wakeup arguments for every wait. Tests need contention:
the runtime cannot create guest threads yet, so use host `pthread`s that each own a hand-made
`Ctx` and thread block (see how `runtime/tests/core/thread_block.c` and the harness set one up),
and run stress cases long enough to mean something (mutual exclusion counters, recursion, try-enter,
many readers and one writer, condition-variable ping-pong with and without timeouts, concurrent
push/pop/flush with a check that no node is lost or duplicated). The x86 memory-ordering mode is
not available in test processes: write the code so it is correct under arm64's weaker ordering.""",
    ),
    dict(
        name="handle", model="sonnet", sections="4.1", placeholder=True, header=True,
        what="the kernel-object handle table",
        build="""\
Implement `rt/handle.h` as RUNTIME.md 4.1: handles are 32-bit values
`(generation << 22) | (index << 2)`, multiples of 4, never 0, never negative, looked up by their
low 32 bits with the low two bits ignored; a table from handle to a reference-counted object with
a kind and an ops table; one namespace for named objects (decide and state, from the Windows
documentation, whether name comparison is case-sensitive, and what happens when a name exists with
a different kind); pseudo-handles `(HANDLE)-1` and `(HANDLE)-2` resolved wherever Windows accepts
them (the thread and process objects do not exist yet: resolve through hooks other units will
fill, and fail loudly until they do); `rt_handle_close` drops the slot and one reference, and the
object dies with its last reference; `rt_handle_dup` makes a new slot for the same object; a
stale handle fails with `ERROR_INVALID_HANDLE` because its generation no longer matches; find-file
handles close only through their own call. The table is used from many threads at once and a
reference taken by one thread must keep the object alive while another closes the handle: say how
your locking guarantees that, and test it with host `pthread`s (each needs its own `Ctx` for last
error; see `runtime/tests/core/thread_block.c`). The design mentions a second table instance for
window handles later: keep the table a value you can instantiate twice.""",
    ),
    dict(
        name="heap", model="sonnet", sections="5.4", placeholder=True, header=True,
        what="the process heap and `HeapCreate` heaps",
        build="""\
Implement `rt/heap.h` as RUNTIME.md 5.4: the process heap is a dedicated host malloc zone (not the
default zone) and each created heap is another zone, destroyed whole; every block has a 16-byte
header `{ magic, heap id, requested size }` and the caller gets the 16-byte-aligned address after
it; size queries return the requested size exactly; free and reallocate check the magic and the
heap id and trap on a foreign pointer; `HEAP_ZERO_MEMORY` zeroes exactly the requested bytes,
including the growth on reallocation; a heap handle is the guest address of a small heap header
checked by a magic number, not a handle-table entry; `rt_galloc` and `rt_gfree` give runtime code
guest-visible memory from the process heap; the process heap's address is written into the
process block (`RT_PEB_PROCESS_HEAP` through `rt_process_block()`). Decide from the Windows
documentation what each flag the header mentions does (`HEAP_NO_SERIALIZE`,
`HEAP_GENERATE_EXCEPTIONS`, `HEAP_REALLOC_IN_PLACE_ONLY`) and implement or trap, never ignore
silently. Sizes of zero, reallocating to zero, a null pointer to free, and allocation failure
(report as Windows does) each need a stated rule and a test. The design assumes zone addresses stay
below 8 TB: check every block and abort with a clear message if one does not. Thread-safe.""",
    ),
    dict(
        name="path", model="sonnet", sections="7.1 and 7.2", placeholder=True, header=True,
        what="translation between the guest's Windows paths and host paths",
        build="""\
Implement `rt/path.h` as RUNTIME.md 7.1 and 7.2: the guest sees only the small virtual `C:` tree
of the mapping table (install root, `C:\\Users\\Player` under the state directory, the temp
directory, an empty `C:\\Windows`), read from `rt_config()`; anything else is
`ERROR_PATH_NOT_FOUND`. Accept drive-absolute, rooted and relative paths (relative to the guest's
current directory, which is runtime state you own, not the host's), both slashes, `.` and `..`
resolved textually as Windows does (including `..` at the root), trailing dots and spaces as
Windows treats them, the `\\\\?\\` prefix, and `NUL` as `/dev/null`; UNC paths, other drives and
device paths fail and are logged. A guest must never be able to name a host file outside the
mapped roots, whatever mixture of `..`, slashes and prefixes it uses: test that adversarially.
Case: ask each mapped root whether its volume is case-sensitive; if not, names pass through; if
it is, resolve each component by exact match first and then by an ASCII case-folded lookup in a
cached directory listing, invalidated when the directory's modification time changes or the
runtime itself creates, deletes or renames in it. This Mac's disk is case-insensitive, so make the
probe's answer overridable from a test and test the folded path that way. The `A` entry point's
bytes are code page 1252: convert them yourself (the locale unit does not exist in your worktree),
and say what you do with the five bytes 1252 leaves undefined. Also the reverse mapping,
`rt_path_to_win_w`, and `rt_win32_from_errno` contexts if the header assigns them to you.""",
    ),
    dict(
        name="locale", model="sonnet", sections="8.4", placeholder=False, header=False,
        what="code-page conversion, character classification, case mapping and the en-US locale data",
        build="""\
There is no header yet: create `runtime/include/rt/locale.h`, `runtime/core/locale.c` and define
`rt_locale_init` (a weak no-op in `core/init.c` today; yours replaces it by being linked). Design
the interface for the shims that will sit on it: `MultiByteToWideChar`, `WideCharToMultiByte`,
`GetACP`, `GetOEMCP`, `GetCPInfo`, `IsValidCodePage`, `GetStringTypeW`, `LCMapStringW`,
`CompareStringW`, `GetLocaleInfoW`, `GetUserDefaultLCID`, `IsValidLocale`, `EnumSystemLocalesW`,
`GetDateFormatW`, `GetTimeFormatW`. As RUNTIME.md 8.4: the machine is en-US, locale `0x0409`,
ANSI code page 1252, OEM 437; conversions are our own code for 1252, 437, 20127, 28591 and UTF-8
(65001) with the Windows rules for invalid input (`MB_ERR_INVALID_CHARS` and without it),
default characters and the used-default flag, the length-query call (zero output size), `-1`
lengths including the terminator, overlapping and too-small buffers and the exact error codes;
any other code page fails with `ERROR_INVALID_PARAMETER` and one log line. Classification
(`CT_CTYPE1`, `2`, `3`) and case mapping are table-driven, exact through Latin Extended-B plus
Greek and Cyrillic case pairs. `CompareString` compares case-folded UTF-16 code units. Locale,
date and time queries answer from an en-US table and fail on anything not in it. **Never use host
`wchar_t`, `mbstowcs`, `iconv`, `setlocale` or `toupper`-style functions.** For the 1252 and 437
byte tables you have an independent check on this machine: Python's `cp1252` and `cp437` codecs
(`.venv/bin/python`); generate the expected tables for your tests from them, and state where
Windows differs (Windows maps the five bytes 1252 leaves undefined to the same-valued control
code points; the codec refuses them). UTF-8: reject overlong forms, surrogates and values above
U+10FFFF as Windows does, and produce surrogate pairs correctly in both directions.""",
    ),
    dict(
        name="time", model="haiku", sections="8.1", placeholder=True, header=True,
        what="the runtime's two clocks",
        build="""\
Implement `rt/time.h` as RUNTIME.md 8.1: `rt_time_mono_100ns()` (continuous, includes time asleep)
and `rt_time_utc_100ns()` (wall clock in Windows `FILETIME` units: 100 ns since 1601-01-01 UTC),
and whatever arithmetic helpers the header declares on top of them: the performance counter at
10,000,000 ticks per second, tick counts as milliseconds since runtime start plus ten minutes,
`rt_rdtsc` as the monotonic clock scaled to a nominal 3.0 GHz, and the time zone as the host's
current offset with no daylight-saving rule. Every other time function in the runtime will be
arithmetic on these two, so they must be monotonic (test it across many calls and threads), have
no overflow for decades of uptime (do the scaling in 128-bit or split arithmetic and say why it
cannot overflow), and agree with each other. Check the epoch conversion against known dates
(1601-01-01, 1970-01-01, a leap day, today). If the header declares a way to substitute a fixed or
recorded clock for tests and trace comparison, implement and test it.""",
    ),
    dict(
        name="machine", model="haiku", sections="8 (intro) and 8.5", placeholder=True, header=True,
        what="the description of the pretend Windows machine",
        build="""\
`runtime/core/machine.c` exists with part of the description; finish it against `rt/machine.h`:
`rt_cpuid`, `rt_machine_system_info`, `rt_machine_processor_feature` and anything else the header
declares that still calls `RT_CORE_UNIMPLEMENTED`. The answers, from RUNTIME.md 8.5: Windows 7
SP1 x64 (version 6.1, build 7601, "Service Pack 1", workstation); x64, family 6, one package, no
SMT; processor count = the host's performance cores (`hw.perflevel0.logicalcpu`), overridable
with `RT_CPUS`; total memory = host physical memory, available = half of it; 4 KB pages, 64 KB
allocation granularity, addresses `0x10000` to `0x7FFFFFEFFFF`. **`rt_cpuid` must report exactly
the feature set `docs/DESIGN.md` section 7 specifies** (read it and `docs/findings/16-cpu-features.md`,
which lists every CPUID leaf the game asks for and which bits it tests): leaves the game queries
must have defined answers, features the lifter does not translate must be reported absent
(including AVX, AES-NI and the others finding 16 names), and `rt_machine_processor_feature` must
agree with it bit for bit (`PF_FASTFAIL_AVAILABLE` is FALSE). Everything that describes the
machine must come from this one file so the answers cannot disagree: test the consistency
(processor count in every place it appears, features in CPUID versus `PF_*`, version numbers).
The same `rt_cpuid` symbol also exists in `harness/driver.c` for the isolated test harness; the
two are never linked together, but they must give the same answers: compare yours with that one
and report any difference instead of changing the harness.""",
    ),
    dict(
        name="env", model="haiku", sections="8.2", placeholder=False, header=False,
        what="the guest's environment block, command line and start-up information",
        build="""\
There is no header yet: create `runtime/include/rt/env.h`, `runtime/core/env.c` and define
`rt_env_init` (a weak no-op in `core/init.c` today; yours replaces it by being linked). Design the
interface for the shims that will sit on it: `GetEnvironmentStringsW`, `FreeEnvironmentStringsW`,
`GetEnvironmentVariableA/W`, `SetEnvironmentVariableA/W`, `GetCommandLineA/W`, `GetStartupInfoW`.
As RUNTIME.md 8.2: the environment is built by the runtime, not inherited: a fixed set
(`SystemRoot=C:\\Windows`, `USERPROFILE=C:\\Users\\Player`, `TEMP` and `TMP`, `PATH`,
`NUMBER_OF_PROCESSORS` from the machine description, `PROCESSOR_ARCHITECTURE=AMD64`,
`OS=Windows_NT`, `COMPUTERNAME`, `USERNAME` and the like) plus overrides from the runtime's
configuration; the command line is `"C:\\Titanfall2\\Titanfall2.exe"` followed by the host
program's arguments, quoted by the Windows rules so that the C runtime's parser gets the same
arguments back (test that round trip with spaces, quotes and backslashes); start-up information
reports no inherited handles. Windows rules to get right and test: variable names compare
case-insensitively; setting a variable to NULL deletes it; the block is a sequence of `NAME=value`
strings ending in an empty string; the get call's return value with a too-small buffer is the
size needed including the terminator; a missing variable sets `ERROR_ENVVAR_NOT_FOUND`.
Content is ASCII, so convert between the 8-bit and 16-bit forms yourself (no host `wchar_t`, and
the locale unit does not exist in your worktree). Memory the guest sees must come from
`rt_vm_alloc` (which works in the skeleton), not from `rt_galloc` (another unit's, unbuilt in your
worktree) and not from `malloc`. Thread-safe.""",
    ),
]

BRIEF = """# Unit rt-core-{name}: {what}

This project statically recompiles a 64-bit Windows game to native arm64 macOS: the game's code
becomes C, and Windows becomes a runtime we write. `docs/RUNTIME.md` is that runtime's design
and `runtime/` holds its skeleton: headers, the shim generator, a test harness, dispatch, and
placeholder subsystems that fail loudly. Your job is one subsystem: {what}. About two hundred
Windows API shims will later be written on top of the core by other workers, so what you build
has to be right in the cases they will lean on, and loud in the cases it does not handle.

## Where you work

- Your worktree is the directory you were given (`.wt/rt-core-{name}`), branch
  `swarm/rt-core-{name}`. Work only inside it. Commit at every point where `make rt-test`
  passes: the account's usage limit can cut you off without warning and only committed work is
  safe. Do not push, merge or switch branches.
- **You own exactly these files and edit no others:**
{owned_list}
  Eight other workers are building the neighbouring subsystems at the same time, each in its own
  files. If a shared file is wrong or lacks something you need (a type in `rt/win/`, a
  declaration in another header, a rule in `rt.mk`, the test harness), do not edit it: work
  around it inside your own files if you can, and say so in your report.
- Python is `.venv/bin/python`. Never run a Python script that uses `multiprocessing` from stdin
  (`python - <<EOF`). Never read or copy anything from `game/`; you do not need the game.
- This Mac: M3 Pro, arm64, macOS 27, 16 KB pages. C17 with GNU extensions,
  `-fno-strict-aliasing -Wall -Wextra -Werror`. No Objective-C.

## Read first

1. `docs/RUNTIME.md`: the introduction and section 0, then section {sections} closely (your
   contract), then sections 3.3 (types), 3.4 (last error), 9.2 (what a function may do when it
   does not handle a case) and 10 (layout, tests).
2. {header_line}
3. `docs/handoff/rt-skeleton.md` sections 2, 3, 4 and 7: what exists, what is declared but not
   built, where the skeleton departs from the design, and the ownership rule you are under.
4. `runtime/core/init.c` (start-up order; where your init function is called), `rt/thread.h`
   (per-thread state goes in your slot of `RtThread.sub[]`; you do not edit that header),
   `rt/log.h`, `rt/test.h` and two existing tests under `runtime/tests/core/` for the idiom.

## What to build

{build}

General rules:

- **No silent defaults.** A case you do not handle must fail loudly and name the offending value
  (the skeleton's unimplemented-function report), never return a plausible answer. A wrong
  answer here turns into corruption far away; a loud stop is cheap to fix.
- **Guest-visible data uses the fixed-width Windows types** of `rt/wintypes.h` and `rt/win/`.
  `long`, `unsigned long`, `wchar_t` and `size_t` never appear in a guest-visible struct or in
  an interface a shim will call.
- Say in a comment, at each place a Windows rule decides the behaviour, what the rule is. Where
  you are not sure what Windows does, say so in the comment and in your report rather than
  guessing quietly.
{placeholder_rule}
## Tests

`runtime/tests/core/{name}.c` (and further `{name}_*.c` files if one gets long). Your own tests
are a weak oracle and an independent reviewer will write more against the Windows documentation
afterwards, so aim to leave them little to find: every documented failure return, every boundary
(zero sizes, maximum values, exact fits, off-by-one on each limit), every flag combination your
interface accepts, and the concurrent cases if your subsystem is used from several threads. A
test that only shows the happy path works is not finished. Core tests use only the test-only
library under `runtime/tests/abi/`, never a product shim.

## Done

On a clean, committed worktree all of these exit 0:

{check_block}

## Report

Reply with: which "Done" commands pass; every place you departed from `docs/RUNTIME.md` or from
the header and why; every Windows behaviour you were unsure of; what you did not test; and
anything wrong you found in shared files. If you run out of room, commit what works and say
exactly what is missing: an honest partial result is useful, a claimed-complete one that fails
its check is not.
"""


def owned(u):
    n = u["name"]
    files = ["runtime/core/%s.c" % n, "runtime/tests/core/%s.c (and %s_*.c)" % (n, n)]
    if not u["header"]:
        files.insert(0, "runtime/include/rt/%s.h (new)" % n)
    if u["placeholder"]:
        files.append("runtime/tests/core/unbuilt_%s.c (delete it)" % n)
    return files


def checks(u):
    n = u["name"]
    allowed = r"^runtime/(core/%s\.c|tests/core/(unbuilt_)?%s(_[a-z0-9_]+)?\.c%s)$" % (
        n, n, "" if u["header"] else r"|include/rt/%s\.h" % n)
    cmds = ["test -z \"$(git diff --name-only $(git merge-base HEAD wip/lifter-v0) HEAD | grep -v -E '%s')\"" % allowed,
            "test -s runtime/tests/core/%s.c" % n]
    if u["placeholder"]:
        cmds.append("test ! -e runtime/tests/core/unbuilt_%s.c" % n)
    if not u["header"]:
        cmds.append("test -s runtime/include/rt/%s.h" % n)
    cmds += ["make rt", "make rt-test"]
    return cmds


def main():
    for i, u in enumerate(UNITS):
        n = u["name"]
        uid = "rt-core-" + n
        cmds = checks(u)
        header_line = ("`runtime/include/rt/%s.h`: the interface you implement, already declared. Implement it as "
                       "written; if it is wrong or incomplete, report that instead of editing it. And "
                       "`runtime/core/%s.c`, the placeholder you replace." % (n, n)) if u["header"] else (
                       "The neighbouring headers `runtime/include/rt/heap.h`, `rt/path.h` and `rt/machine.h` as "
                       "models of style: you write `rt/%s.h` yourself, with a comment on every declaration that "
                       "a newcomer could implement a shim from." % n)
        placeholder_rule = ("- Delete `runtime/tests/core/unbuilt_%s.c`: it asserts that your functions are unbuilt. "
                            "No function of your interface may still end in the unbuilt-function report unless the "
                            "design says that case traps, and then say which in your report.\n" % n) if u["placeholder"] else ""
        brief = BRIEF.format(name=n, what=u["what"], sections=u["sections"], build=u["build"],
                             owned_list="\n".join("  - `%s`" % f for f in owned(u)), header_line=header_line,
                             placeholder_rule=placeholder_rule, check_block="\n".join("    " + c for c in cmds))
        with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
            f.write(brief)
        path = os.path.join(ROOT, "swarm", "units", uid + ".json")
        if os.path.exists(path):
            continue
        unit = {"id": uid, "title": "Runtime core: " + u["what"], "kind": "runtime-core", "issue": 24,
                "model": u["model"], "priority": 15, "needs": ["rt-skeleton"], "check": cmds, "state": "todo"}
        with open(path, "w") as f:
            json.dump(unit, f, indent=2)
            f.write("\n")
        print("wrote", uid, u["model"])


if __name__ == "__main__":
    main()
