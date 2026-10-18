#!/usr/bin/env python3
"""Write the ledger units and briefs that apply the "wrong" rows of a Windows-behaviour review.

A review under docs/reviews/ lists, under "Wrong, described for a Mac-side fix", one block per
behaviour: where, what we do today, what Windows does, the source and a test case.  Each
block becomes one Haiku task: the block is the brief's centre, word for word.

    .venv/bin/python tools/swarm/make_winfix_tasks.py            write swarm/units and swarm/briefs
    .venv/bin/python tools/swarm/make_winfix_tasks.py --show ID  print one brief

ROWS says which blocks are given out; a block left out stays as it is, with the reason here.
A unit file that exists is left alone; briefs are rewritten.  Run with tools/swarm/wave.py
--kind winfix-task.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# (review file, block label, unit id, orchestrator's note appended to the block)
ROWS = [
    ("docs/reviews/win-behaviour-check-2.md", "W1", "winfix-resume-ended",
     "Also look at `rt_thread_suspend` for an ended thread: per the same test Windows answers "
     "`(DWORD)-1`. If it stops or answers otherwise today, make it answer -1 and test it; the "
     "last error for either call is not known, so leave it alone and say so in the comment."),
    ("docs/reviews/win-behaviour-check-2.md", "W2", "winfix-fls-get",
     "The body is not written yet (`RT_CORE_UNIMPLEMENTED`). Correct the specification in "
     "`rt/thread.h` and the line in `runtime/README.md`; do not write the body. No new test: "
     "put the test case into the header comment as the case the body's author must add."),
    ("docs/reviews/win-behaviour-check-2.md", "W3", "winfix-fls-set",
     "The body is not written yet. Correct the specification in `rt/thread.h` only (the stop "
     "goes; the store succeeds); do not write the body. Put the test case into the header "
     "comment, and the open point about a later `FlsAlloc`, marked not verified."),
    ("docs/reviews/win-behaviour-check-2.md", "W4", "winfix-heapalloc-error", ""),
    ("docs/reviews/win-behaviour-check-2.md", "W5", "winfix-ctype1-defined", ""),
    ("docs/reviews/win-behaviour-check-2.md", "W6", "winfix-isvalidlocale", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X1", "winfix-lcmap-flags", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X2", "winfix-codepage-order",
     "Decision: any code page this runtime does not carry is answered ERROR_INVALID_PARAMETER "
     "before the flags are looked at. For a real Windows page we do not carry (932 in the "
     "existing tests) that differs from Windows only when the flags are bad as well: say so in "
     "the comment, and change the two existing tests to this order. Use 0xdeadbeef as the "
     "unknown page in the new test."),
    ("docs/reviews/win-behaviour-check-3.md", "X3", "winfix-dateformat-empty", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X4", "winfix-nouseroverride", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X5", "winfix-affinity-all", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X6", "winfix-heaprealloc-null", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X7", "winfix-heapfree-null", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X9", "winfix-drivetype-forms", ""),
    ("docs/reviews/win-behaviour-check-3.md", "X10", "winfix-rmdir-current", ""),
    # X8 (LoadLibraryEx("") error code) is left: it rests on one "win8" comment in Wine's test.
    ("docs/reviews/win-behaviour-check-4.md", "Y2", "winfix-getenv-empty", ""),
    ("docs/reviews/win-behaviour-check-4.md", "Y4", "winfix-process-affinity", ""),
    ("docs/reviews/win-behaviour-check-4.md", "Y5", "winfix-fixed-heap-limit",
     "The limit is decided: a block must be smaller than 0xFF000 bytes (1,024 KB less one 4 KB "
     "page, our reading of the page's 'slightly less than 1,024 KB'). Say in the comment that "
     "the exact figure is ours and that only the two points of Wine's test (0x80000 fits, "
     "0x100000 does not) are known of Windows."),
    # Y1 (linguistic casing of dotted and dotless i), Y3 (genitive month names), Y6
    # (DuplicateHandle of a pseudo-handle, or inheritable) and Y7 (views and mappings with no
    # access) stay stops: each is a stop today, which is honest, and none has a caller yet.
    ("docs/reviews/win-behaviour-check-5.md", "Z1", "winfix-getaddrinfo-spec",
     "First look whether the function's body is written. If it is still "
     "`RT_CORE_UNIMPLEMENTED`, correct the specification in the header comment only, put the "
     "block's test cases there as the cases the body's author must add, and write no body and "
     "no test. If the body exists, change it and add the test as the rules below say."),
    ("docs/reviews/win-behaviour-check-5.md", "Z4", "winfix-socket-checks",
     "First look whether the function's body is written. If it is still "
     "`RT_CORE_UNIMPLEMENTED`, correct the specification in the header comment only, put the "
     "block's test cases there as the cases the body's author must add, and write no body and "
     "no test. If the body exists, change it and add the test as the rules below say."),
    ("docs/reviews/win-behaviour-check-5.md", "Z5", "winfix-wsastartup-versions",
     "First look whether the function's body is written. If it is still "
     "`RT_CORE_UNIMPLEMENTED`, correct the specification in the header comment only, put the "
     "block's test cases there as the cases the body's author must add, and write no body and "
     "no test. If the body exists, change it and add the test as the rules below say."),
    ("docs/reviews/win-behaviour-check-5.md", "Z6", "winfix-inet-pton",
     "First look whether the function's body is written. If it is still "
     "`RT_CORE_UNIMPLEMENTED`, correct the specification in the header comment only, put the "
     "block's test cases there as the cases the body's author must add, and write no body and "
     "no test. If the body exists, change it and add the test as the rules below say."),
    ("docs/reviews/win-behaviour-check-5.md", "Z7", "winfix-inet-ntop",
     "First look whether the function's body is written. If it is still "
     "`RT_CORE_UNIMPLEMENTED`, correct the specification in the header comment only, put the "
     "block's test cases there as the cases the body's author must add, and write no body and "
     "no test. If the body exists, change it and add the test as the rules below say."),
    ("docs/reviews/win-behaviour-check-5.md", "Z8", "winfix-inet-addr",
     "First look whether the function's body is written. If it is still "
     "`RT_CORE_UNIMPLEMENTED`, correct the specification in the header comment only, put the "
     "block's test cases there as the cases the body's author must add, and write no body and "
     "no test. If the body exists, change it and add the test as the rules below say."),
    # Z2 and Z3 (hooks, GetObject of a DIB section) are the window pieces': passed to unit
    # rt-window-core-1.  Z9 (DOS wildcards in FindFirstFile) stays a stop: no caller yet.
    ("docs/reviews/contracts-check.md", "C6", "winfix-imm-last-error",
     "The specification (`rt/window.h`) and the task rows are already corrected. What is left is "
     "the shims a wave wrote from the old text, and their tests: `runtime/shims/imm32/` ImmGetCompositionStringW, ImmGetCandidateListW, ImmGetCandidateListCountW, ImmGetConversionStatus, ImmSetConversionStatus, ImmNotifyIME, ImmSetCandidateWindow. "
     "Change those shims and tests only; `make shim-check SHIM=<lib>/<Name>` must pass for "
     "each, besides the whole suite."),
    ("docs/reviews/contracts-check.md", "C7", "winfix-imm-ui-message",
     "The specification (`rt/window.h`) and the task rows are already corrected. What is left is "
     "the shims a wave wrote from the old text, and their tests: `runtime/shims/imm32/ImmIsUIMessageA.c`. "
     "Change those shims and tests only; `make shim-check SHIM=<lib>/<Name>` must pass for "
     "each, besides the whole suite."),
    ("docs/reviews/contracts-check.md", "C8", "winfix-rawinput-codes",
     "The specification (`rt/window.h`) and the task rows are already corrected. What is left is "
     "the shims a wave wrote from the old text, and their tests: `runtime/shims/user32/` GetRawInputDeviceList and GetRawInputDeviceInfoA. "
     "Change those shims and tests only; `make shim-check SHIM=<lib>/<Name>` must pass for "
     "each, besides the whole suite."),
    # W7 (GetTimeFormat with TIME_* flags and a picture) stays a stop: the review's four
    # expected strings do not pin the separator rule.
]

BRIEF = """# Unit {id}: one Windows behaviour our runtime has wrong ({label} of a review)

This repository recompiles a 64-bit Windows game for arm64 macOS. `runtime/` is our own
implementation of the Windows functions the game calls: `runtime/core/` is the machinery,
`runtime/include/rt/` its headers, `runtime/shims/` one small C function per Windows
function, `runtime/tests/` the tests. A review compared behaviours that had been written
from memory with public sources and found this one wrong. Apply it exactly; it is a
decision, not a suggestion.

## Read first

`runtime/README.md` (the rules, about 115 lines), then only the files the block names.

## The finding (from `{review}`, word for word)

{block}
{note}
## Rules

- Change the function named, the comment in its header that states the old behaviour, and
  a shim's header block if it repeats the old behaviour (`grep -rn` the function's name
  under `runtime/shims/`). Nothing else.
- Add or extend a test under `runtime/tests/` (beside the existing tests of that function:
  find them with `grep -rln`) that fails without your change and passes with it, built
  from the block's "Test" line. A test checks what the guest can see: return values, the
  last error (also that a success leaves it alone where the block does not say otherwise),
  every byte written.
- If an existing test asserts the old behaviour, correct it and say in its comment what
  Windows does, with the block's source.
- Keep every other test passing. If the change cannot be made as written (the code is not
  as the block describes, a constant is missing), do not work around it: change nothing
  and say exactly what you found.
- You have no game files and need none. Do not read `.env`; do not push; commit in this
  worktree when the check passes.

## Done

On a clean, committed worktree (pipe output through `tail -15`):

    make rt-test
"""


def block_of(review, label):
    text = open(os.path.join(ROOT, review)).read()
    m = re.search(r"^\*\*%s\. .*?(?=^\*\*[A-Z]\d+\. |^## )" % re.escape(label), text, re.S | re.M)
    if not m:
        raise SystemExit(f"{review}: no block {label}")
    return m.group(0).rstrip() + "\n"


def brief_of(row):
    review, label, uid, note = row
    return BRIEF.format(id=uid, label=label, review=review, block=block_of(review, label),
                        note=("\nOrchestrator's note: " + note + "\n") if note else "")


def main():
    if "--show" in sys.argv:
        want = sys.argv[sys.argv.index("--show") + 1]
        for row in ROWS:
            if row[2] == want:
                print(brief_of(row))
                return 0
        print("no such row", file=sys.stderr)
        return 1
    wrote = 0
    for row in ROWS:
        review, label, uid, _ = row
        with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
            f.write(brief_of(row))
        path = os.path.join(ROOT, "swarm", "units", uid + ".json")
        if os.path.exists(path):
            continue
        unit = {"id": uid, "title": f"Runtime: fix a Windows behaviour found wrong by review ({label})",
                "kind": "winfix-task", "issue": 24, "model": "haiku", "priority": 20, "needs": [],
                "check": ["make rt-test"], "state": "todo", "attempts": 0, "history": []}
        with open(path, "w") as f:
            json.dump(unit, f, indent=2, sort_keys=True)
            f.write("\n")
        wrote += 1
    print(f"{len(ROWS)} brief(s) written, {wrote} new unit file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
