#!/usr/bin/env python3
"""Haiku tasks out of the "Mechanical follow-ups" table of a shim review.

A sampled review (docs/reviews/shim-wave*-review.md) ends with a table `| # | File | Change |`:
one exact change per row, with the test to add.  Each row becomes one task; a row marked
**(planner)** needs a decision first and gets no task until it is listed in DECIDED with the
decision.  A unit file that exists is left alone; briefs are rewritten.

    .venv/bin/python tools/swarm/make_tablefix_tasks.py            write swarm/units and swarm/briefs
    .venv/bin/python tools/swarm/make_tablefix_tasks.py --list     rows, and which are the planner's
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REVIEWS = [("docs/reviews/shim-wave3-review.md", "wave3fix"),
           ("docs/handoff/rt-census-rows.md", "censusfix")]   # the four rows review census-rows leaves to the planner
DECIDED = {
    ("docs/reviews/shim-wave3-review.md", 1):
        "do NOT add the stop the row proposes. Equality, the common use, is right under "
        "code-unit order, and a stop would end runs that only test for equality. Instead: the "
        "first time two unequal strings are compared of which either has a byte outside 0-9 and "
        "a-z, write one WARN line (`rt_log_once`) saying that the order is by code unit and not "
        "Windows' word sort, and return the code-unit result as today. RT-STATUS `partial`; "
        "RT-CASES gains `unequal strings outside 0-9a-z: code-unit order, one warning`. Test: "
        "`lstrcmpA(\"a\", \"B\")` returns what it returns today and the warning is logged once "
        "over two calls. Leave the row file alone.",
}        # ("docs/reviews/...", row number) -> the planner's decision, appended to the row

BRIEF = """# Unit {uid}: one follow-up of a shim review (row {n} of its table)

This repository recompiles a 64-bit Windows game for arm64 macOS. `runtime/` is our own
implementation of the Windows functions the game calls: one small C function per Windows
function under `runtime/shims/<library>/`, its test under `runtime/tests/shims/<library>/`,
the machinery they sit on under `runtime/core/`. A reviewer read the shims written
yesterday and today and listed exact follow-ups. This is one of them. Apply it exactly; it
is a decision, not a suggestion.

## Read first

`runtime/README.md` (the rules for a shim and its test, about 115 lines), then the file
the row names and its test.

## The row (from `{review}`, table "Mechanical follow-ups")

File: {file}

{change}
{decision}
The finding behind it, if you need the reason, is in the same review under "Findings":
search it for the function's name. Read only that finding, not the whole review.

## Rules

- Change the file the row names, its test, and the shim's header block (`RT-STATUS`,
  `RT-CASES`, `RT-TRAPS`) where the change makes it say something untrue. Nothing else,
  unless the row itself names another file.
- The test must fail without your change and pass with it. A test checks what the guest
  can see: return values, the last error (also that a success leaves it alone where the
  row does not say otherwise), every byte written.
- If the code is not as the row describes (the line moved is fine; the behaviour already
  changed is not), change nothing and say exactly what you found.
- You have no game files and need none. Do not read `.env`; do not push; commit in this
  worktree when the check passes.

## Done

On a clean, committed worktree (pipe output through `tail -15`):

    make rt-test
"""


def rows(review):
    text = open(os.path.join(ROOT, review)).read()
    tail = text[text.index("## Mechanical follow-ups"):]
    out = []
    for line in tail.splitlines():
        m = re.match(r"^\| *(\d+) *\| *(.*?) *\| *(.*) *\|\s*$", line)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3).replace("\\|", "|")))
    return out


def main():
    listing = "--list" in sys.argv
    wrote = made = planner = 0
    for review, prefix in REVIEWS:
        for n, file, change in rows(review):
            held = "(planner)" in change and (review, n) not in DECIDED
            if listing:
                print("%3d %-8s %s" % (n, "PLANNER" if held else "task", file[:70]))
                continue
            if held:
                planner += 1
                continue
            uid = "%s-%02d" % (prefix, n)
            dec = DECIDED.get((review, n))
            brief = BRIEF.format(uid=uid, n=n, review=review, file=file, change=change,
                                 decision=("\nThe planner's decision on this row: " + dec + "\n") if dec else "")
            with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
                f.write(brief)
            wrote += 1
            path = os.path.join(ROOT, "swarm", "units", uid + ".json")
            if os.path.exists(path):
                continue
            unit = {"id": uid, "title": "Runtime: review follow-up %d (%s)" % (n, file[:60].replace("`", "")),
                    "kind": "winfix-task", "issue": 57, "model": "haiku", "priority": 21, "needs": [],
                    "check": ["make rt-test"], "state": "todo", "attempts": 0, "history": []}
            with open(path, "w") as f:
                json.dump(unit, f, indent=2, sort_keys=True)
                f.write("\n")
            made += 1
    if not listing:
        print("%d brief(s) written, %d new unit file(s), %d row(s) wait for the planner" % (wrote, made, planner))
    return 0


if __name__ == "__main__":
    sys.exit(main())
