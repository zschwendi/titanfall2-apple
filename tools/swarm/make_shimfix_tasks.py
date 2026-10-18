#!/usr/bin/env python3
"""Write the ledger units and briefs that apply a shim review's follow-ups.

    python3 tools/swarm/make_shimfix_tasks.py            write swarm/units and swarm/briefs
    python3 tools/swarm/make_shimfix_tasks.py --show ID  print one brief

Each task changes a few existing shims and their tests as a review's table says
(docs/reviews/shim-wave2-review.md, "Mechanical follow-ups"), with the planner's decisions on
the findings the review left open (NOTES.md, 2026-10-09 18:50).  A unit file that exists is
left alone, so a re-run does not disturb the ledger; briefs are rewritten.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

TASKS = [
    dict(id="wc2mb-tags", shims=["WideCharToMultiByte"], text="""\
The shim forwards to the core (`rt_wide_to_mb`, `runtime/include/rt/locale.h`), and the core
stops the run in four cases the shim's header does not list. Nothing in the shim's code
changes. Change its header block and its test:

- `RT-STATUS: partial`
- `RT-TRAPS:  core: a character a code page other than 1252 lacks, without WC_NO_BEST_FIT_CHARS; core: WC_DISCARDNS, WC_SEPCHARS or WC_DEFAULTCHAR; core: WC_COMPOSITECHECK with a combining mark; core: partially overlapping buffers`
- In the test, one `T_TRAPS` per entry, each a `CALL(kernel32, WideCharToMultiByte, ...)`:
  1. code page 437, flags 0, the one character U+0101: text `"no direct mapping"`.
  2. code page 1252, flags `WC_DISCARDNS`, the text `L"a"`: text `"WC_DISCARDNS"`.
  3. code page 1252, flags `WC_COMPOSITECHECK`, the one character U+0301: text `"combining mark"`.
  4. code page 1252, flags 0, source and destination overlapping partially (a `WCHAR` array
     of 8 holding `L"abc"`, destination `(char *)array + 1`, 8 bytes): text `"overlap"`.
- Code page 1252 itself no longer stops for a character it lacks: with flags 0 Windows's
  best-fit table is applied. Add to the test, as ordinary checks: `WideCharToMultiByte(1252, 0,
  ...)` of U+0101 writes the byte `'a'` and leaves `*lpUsedDefaultChar` FALSE; of U+4E2D writes
  `'?'` and sets it TRUE. If the test has a case that expects otherwise, correct that case.
"""),
    dict(id="core-stop-tags", shims=["WaitForMultipleObjects", "GetStartupInfoW"], text="""\
Two shims whose header says less than the core does. No code changes; header and test only.

`WaitForMultipleObjects`: `RT-STATUS: partial`; `RT-TRAPS:  core: NULL lpHandles`. Add to the
test: `T_TRAPS(CALL(kernel32, WaitForMultipleObjects, 1, NULL, FALSE, 0), "NULL handle array");`

`GetStartupInfoW`: `RT-STATUS: partial`; remove the words "; NULL lpStartupInfo is fatal" from
`RT-CASES`, and remove the `T_CASE` line of the test that has that text while keeping the
`T_TRAPS` under it (move the `T_TRAPS` into the neighbouring case); `RT-TRAPS:  core: NULL lpStartupInfo`.
"""),
    dict(id="semaphore-names", shims=["CreateSemaphoreW", "CreateSemaphoreA"], text="""\
`CreateSemaphoreW`: no code change. `RT-STATUS: partial`; `RT-TRAPS:  core: an empty name;
core: a backslash in the name; core: a name longer than MAX_PATH`. Test: three `T_TRAPS`, one
per entry, with the texts `"empty object name"`, `"backslash"` and `"code units"` (names
`L""`, `L"a\\\\b"`, and 261 times `L'x'`).

`CreateSemaphoreA`:
- The name buffer is one too short: a name of exactly `MAX_PATH` (260) characters is refused
  where `OpenEventA` and the core accept it. Make the buffer `WCHAR wide[MAX_PATH + 1];` and
  pass `MAX_PATH + 1` as the size to `rt_mb_to_wide`. In `RT-CASES` say "a name of MAX_PATH
  characters is accepted; a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE"
  (the wording of `runtime/shims/kernel32/OpenEventA.c`, which is the shim to copy from), and
  test both: 260 characters succeeds and the handle closes; 261 fails with that error.
- `RT-STATUS: partial`; `RT-TRAPS:  core: an empty name; core: a backslash in the name`.
  Test: two `T_TRAPS`, texts `"empty object name"` and `"backslash"`.
"""),
    dict(id="event-mutex-names", shims=["CreateEventA", "CreateMutexA", "OpenEventA"], text="""\
`CreateEventA` and `CreateMutexA`:
- The name buffer is one too short: a name of exactly `MAX_PATH` (260) characters is refused
  where `OpenEventA` and the core accept it. Make the buffer `WCHAR wide[MAX_PATH + 1];` and
  pass `MAX_PATH + 1` as the size to `rt_mb_to_wide`. In `RT-CASES` say "a name of MAX_PATH
  characters is accepted; a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE"
  (the wording of `OpenEventA.c`), and test both: 260 characters succeeds and the handle
  closes; 261 fails with that error.
- `RT-TRAPS:  an empty name; core: a backslash in the name` for both. `CreateEventA`'s test
  already has both `T_TRAPS`; to `CreateMutexA`'s add
  `T_TRAPS(CALL(kernel32, CreateMutexA, NULL, FALSE, "a\\\\b"), "backslash");`

`OpenEventA`: no code change. `RT-TRAPS:  an empty name; core: a backslash in the name`. If no
`T_TRAPS` of its test uses a name with a backslash, add
`T_TRAPS(CALL(kernel32, OpenEventA, EVENT_ALL_ACCESS, FALSE, "a\\\\b"), "backslash");`
"""),
    dict(id="locale-guards", shims=["CompareStringW", "GetDateFormatW", "GetTimeFormatW"], text="""\
Three shims stop the run for a locale value the core takes. The C runtime passes locale 0.

`CompareStringW`: the guard on `Locale` (about line 28) stops for every locale except 0x0409,
0x0400 and 0x0800. Add `&& Locale != LOCALE_NEUTRAL && Locale != LOCALE_INVARIANT` to its
condition. Test: `CompareStringW(0, NORM_IGNORECASE, L"abc", -1, L"ABC", -1)` returns
`CSTR_EQUAL`, and the same with `LOCALE_INVARIANT`. Mention both in `RT-CASES`.

`GetDateFormatW` and `GetTimeFormatW`: each has a helper `is_en_us` with three alternatives.
Add `locale == LOCALE_NEUTRAL` as a fourth, as `runtime/shims/kernel32/GetLocaleInfoW.c` has
it (lines 12 to 15). Test, in each: one call with locale 0 gives exactly the result (return
value and text) of the same call with 0x0409. Mention it in `RT-CASES`. `LOCALE_INVARIANT`
stays refused in these two.
"""),
    dict(id="lcmap-guards", shims=["LCMapStringW"], text="""\
The shim has eight `RT_UNIMPLEMENTED` guards before the core call. Each repeats a stop the
core (`rt_lcmap_string`) makes itself, and because they run first they turn three documented
error returns into a stop: for example `LCMapStringW(0x409, LCMAP_SORTKEY, NULL, 0, NULL, 0)`
must return 0 with `ERROR_INVALID_PARAMETER`, and stops instead.

- Delete the eight guards and the comment above the `SHIM` line that explains them. The shim
  body becomes the one `return rt_lcmap_string(...)` line.
- `RT-TRAPS:  core: flags 0; core: LCMAP_TITLECASE; core: LCMAP_SORTKEY; core: LCMAP_BYTEREV; core: LCMAP_HASH; core: kana, width and Chinese flags; core: NORM_* flags; core: other flags`
- The test keeps its eight `T_TRAPS` calls, with valid parameters (a source string, length
  -1, a size query). Their texts are now the core's, not the shim's. Run each once to see the
  report the core prints and put a distinctive piece of it in the `T_TRAPS` (the core's
  reports say, among others, "dwMapFlags 0", "LCMAP_TITLECASE", "combine a case mapping",
  "without LCMAP_LOWERCASE or LCMAP_UPPERCASE"). Do not change `runtime/core/`.
- Add three checks of the error returns the guards used to hide:
  `(0x409, LCMAP_SORTKEY, NULL, 0, NULL, 0)` returns 0 with `ERROR_INVALID_PARAMETER`;
  `(0x409, LCMAP_SORTKEY | 0x40, L"abc", -1, NULL, 0)` returns 0 with `ERROR_INVALID_FLAGS`;
  `(0x409, LCMAP_LINGUISTIC_CASING | NORM_IGNORECASE, L"abc", -1, NULL, 0)` returns 0 with
  `ERROR_INVALID_FLAGS`. If the core gives something else for one of them, do not bend the
  test: say which in your reply and leave that check out.
"""),
    dict(id="two-guards", shims=["GetCPInfo", "FreeEnvironmentStringsW"], text="""\
Two shims stop the run for a call the core answers. Delete each guard and test the answer.

`GetCPInfo` (about lines 14 to 15) stops on `CP_UTF8`. The core's `rt_cp_info` answers it:
TRUE, `MaxCharSize` 4. Delete the guard, remove its entry from `RT-TRAPS` (write `none` if it
was the only one, and then `RT-STATUS: full` unless another reason for `partial` is stated),
replace the test's `T_TRAPS` for it by checks of the return value and of `MaxCharSize`, and
say so in `RT-CASES`.

`FreeEnvironmentStringsW` (about line 16) stops on NULL. The core accepts NULL and returns
TRUE (`runtime/include/rt/env.h`, the comment of the function the shim calls). Delete the
guard, fix `RT-TRAPS` and `RT-STATUS` the same way, replace the test's `T_TRAPS` by a check
that the call returns TRUE and leaves the last error alone.
"""),
]

BRIEF = """# Unit shimfix-{id}: review follow-ups for {names}

This repository recompiles a 64-bit Windows game for arm64 macOS. When the game calls a
Windows function the call lands in a *shim*: a small C function that checks the arguments as
Windows documents, calls our runtime's core, and returns the result. A review of the shims
found the points below. Apply them exactly; they are decisions, not suggestions.

## Files (edit exactly these, nothing else)

{file_list}

## Read first

`runtime/README.md` (the rules for a shim and its test, 115 lines), then the files above. A
header entry written `core: <case>` in `RT-TRAPS` names a stop the core makes, not the shim;
the check wants at least as many `T_TRAPS` in the test as there are `RT-TRAPS` entries.

## The changes

{text}
## Rules

- Do not edit `runtime/core/`, `runtime/include/` or any other shim. If a change cannot be
  made as written (a constant is missing, the core answers differently), do not work around
  it: leave that one change out and say exactly what you found.
- A test checks what the guest can see: return values, last error (also that success leaves
  it alone), every byte the function writes.
- Keep every existing test case that is still true.

## Done

Each of these exits 0 on a clean, committed worktree. Run each as you finish that shim and
commit when it passes (pipe the output through `tail -15`):

{check_block}
"""


def build(t):
    uid = "shimfix-" + t["id"]
    names = ["kernel32/" + m for m in t["shims"]]
    cmds = []
    for n in names:
        others = ",".join(x for x in names if x != n)
        cmds.append("make shim-check SHIM=%s%s" % (n, " SHIM_FAMILY=" + others if others else ""))
    files = []
    for m in t["shims"]:
        files += ["runtime/shims/kernel32/%s.c" % m, "runtime/tests/shims/kernel32/%s.c" % m]
    brief = BRIEF.format(id=t["id"], names=", ".join("`%s`" % m for m in t["shims"]),
                         file_list="\n".join("- `%s`" % f for f in files), text=t["text"],
                         check_block="\n".join("    " + c for c in cmds))
    unit = {"id": uid, "title": "Shim review follow-ups: %s" % ", ".join(t["shims"]), "kind": "shim-task",
            "issue": 24, "model": "haiku", "priority": 22, "needs": ["review-shims-2"], "check": cmds, "state": "todo"}
    return uid, brief, unit


def main():
    show = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--show" else None
    for t in TASKS:
        uid, brief, unit = build(t)
        if show:
            if show in (uid, t["id"]):
                print(brief)
            continue
        with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
            f.write(brief)
        path = os.path.join(ROOT, "swarm", "units", uid + ".json")
        if os.path.exists(path):
            continue
        with open(path, "w") as f:
            json.dump(unit, f, indent=2)
            f.write("\n")
        print("wrote %s (%d tokens of brief, roughly)" % (uid, len(brief) // 4))


if __name__ == "__main__":
    main()
