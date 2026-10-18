#!/usr/bin/env python3
"""Tasks for Haiku workers out of the runtime's contract handoffs (docs/handoff/rt-contract-*.md).

A contract unit (threads, loader, crash, files) left a header that is the specification, stub source files, and a handoff with
one section per remaining piece: the text a worker's brief must carry and a numbered test plan.
Every piece marked haiku becomes TWO units here, done by two different workers:

    core-<piece>-test   types the plan into runtime/tests/core/pending/<test>.c, which the build
                        does not compile                                    (kind core-test)
    core-<piece>-body   writes the body, moves the test up unchanged and makes it pass
                        (kind core-body; needs its test unit)

    make_core_tasks.py [--out DIR]    write DIR/briefs/<unit>.md (always) and DIR/units/<unit>.json
                                      (only where none exists); DIR defaults to swarm/
    make_core_tasks.py --list         the table: unit, state in the table, needs, brief size
    make_core_tasks.py --show UNIT    print one brief
    make_core_tasks.py --check        every file a piece names exists, every handoff section and
                                      header marker it pastes from exists, every plan has numbered
                                      cases, no file is named by two pieces; exit 0 if so

Nothing of a brief's specification is typed here: the brief text and the test plan are read
from the handoff by heading, and the header excerpts from the headers by marker, when the
brief is generated.  "Done" for both kinds is tools/swarm/core_check.py, which reads PIECES.

A piece a contract marked opus has no row (docs/handoff/rt-core-tasks.md lists them; the window
contract's are in docs/handoff/rt-window-core-1.md).
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TH, LD, CR = ("docs/handoff/rt-contract-thread.md", "docs/handoff/rt-contract-loader.md",
              "docs/handoff/rt-contract-crash.md")
FL = "docs/handoff/rt-contract-file.md"
INC, CORE, TESTS = "runtime/include/rt/", "runtime/core/", "runtime/tests/core/"

# A "range" is (file, start, end): the lines of `file` from the first line that contains `start`
# (None: the top) up to, not including, the next line that contains `end` (None: the end of the
# file).  ("fn", file, name) is the comment and declaration of one function of a header.
TH_COMMON = [(TH, "- Rules for every test:", "- Done:")]
TH_SPEC = [(INC + "thread.h", " * LAST ERROR (section 3.4", " * WHAT IS NOT VERIFIED"), (CORE + "thread_priv.h", None, None)]
LD_COMMON = [(LD, "Every test file starts with", "### Task 0")]
LD_IMAGE = [(CORE + "loader.h", "---- the image (loader_image.c)", "---- GetProcAddress policy"),
            (INC + "win/pe.h", "---- the image", None)]
LD_PE = [(INC + "test_pe.h", None, None)]
CR_COMMON = [(CR, "Include line for private functions", "| Task | Specification excerpt"),
             (CR, "Conventions for all three.", "### 5.1")]
CR_BODY = [(CR, "Two things a body worker must be told", "## 5. Test plans")]

FL_COMMON = [(FL, "- Copy the shape and the helpers", "- Check of a test task")]
FL_BODY = [(FL, "Every body brief carries, verbatim", "- **rt-file-std.**")]
FL_SPEC = [(INC + "file.h", " * NAMES.  A function that takes a file name", "---- open (core/file.c, built)"),
           (CORE + "file_priv.h", None, None)]
FL_NOTE = ("The text below was written before `T_PLAN` existed: where it says a `T_CASE(\"...\")` before each group "
           "of checks, write every numbered case of the plan as one `T_PLAN` block instead, as this page says; and "
           "the check of this task is the one under \"Done\", not a `-fsyntax-only` line.")


def file_piece(name, what, start, end, **more):
    return dict(id="file-" + name, what=what, handoff=FL, common=FL_COMMON, section=None,
                rows=["- **rt-file-%s.**" % name], rows_after="## Briefs for the haiku body tasks",
                plan="### rt-file-" + name, tag="rt-file-" + name, sources=[CORE + "file_%s.c" % name],
                test="file_" + name, unbuilt=None, spec=[(INC + "file.h", start, end)] + FL_SPEC, common_body=FL_BODY,
                shape_test=TESTS + "file_lifetime.c", shape_body=CORE + "file.c", note=FL_NOTE, **more)


# One row per haiku piece, in the order the work should go (the priority of the units follows it).
#   id        the piece; its units are core-<id>-test and core-<id>-body
#   what      a few words for titles
#   handoff, section   where the piece's brief text and test plan are; `plan` is the heading of
#             the plan when it is a section of its own (the crash handoff), `rows` the table
#             rows of the handoff that are the piece's brief text there
#   tag       the unit-of-work text of the stub's RT_CORE_UNIMPLEMENTED; None: no stub exists,
#             so the test cannot be linked before the body (it is compiled only)
#   sources   the file or files the body task writes
#   test      runtime/tests/core/pending/<test>.c, later runtime/tests/core/<test>.c; every
#             TEST in it is named <test> or <test>_<word>
#   unbuilt   the test that proves the stub is loud, which the body task deletes (or None)
#   also      other paths the body task may change
#   spec, spec_test, spec_body   header excerpts for both briefs, the test's, the body's
#   shape_test, shape_body       the one file to copy the shape from
#   drop      plan cases that are not this piece's (see `note`)
#   needs_test, needs_body       units that must be merged first, besides rt-core-tasks and
#             (for a body) the piece's own test unit
#   keeps_stub  True if a RT_CORE_UNIMPLEMENTED may stay in the sources
#   done      "test" or "body" once that unit was done by hand (the proof run): --list shows it
#             and no unit file is written for a unit that is done
PIECES = [
    dict(id="thread-tls", what="TLS slots", handoff=TH, section="## Row T: TLS slots", common=TH_COMMON,
         tag="rt-thread-tls", sources=[CORE + "thread_tls.c"], test="thread_tls", unbuilt=TESTS + "unbuilt_thread_tls.c",
         spec=[(INC + "thread.h", "---- TLS slots", "---- FLS slots")] + TH_SPEC,
         shape_test=TESTS + "thread_run.c", shape_body=CORE + "thread_run.c", done="body"),
    file_piece("std", "standard handles and the console control handler",
               "---- standard handles and the console", "---- file mappings"),
    dict(id="thread-ctl", what="looking at and adjusting a thread", handoff=TH, common=TH_COMMON,
         section="## Row C: looking at and adjusting a thread",
         tag="rt-thread-ctl", sources=[CORE + "thread_ctl.c"], test="thread_ctl", unbuilt=TESTS + "unbuilt_thread_ctl.c",
         spec=[(INC + "thread.h", "---- looking at and adjusting a thread", "---- host threads")] + TH_SPEC,
         shape_test=TESTS + "thread_run.c", shape_body=CORE + "thread_run.c"),
    dict(id="thread-attach", what="host threads the runtime did not create", handoff=TH, common=TH_COMMON,
         section="## Row A: host threads the runtime did not create",
         tag="rt-thread-attach", sources=[CORE + "thread_attach.c"], test="thread_attach",
         unbuilt=TESTS + "unbuilt_thread_attach.c",
         spec=[(INC + "thread.h", "---- host threads", "---- TLS slots")] + TH_SPEC,
         shape_test=TESTS + "thread_run.c", shape_body=CORE + "thread_run.c"),
    dict(id="process", what="the process: priority class and affinity masks", handoff=TH, common=TH_COMMON,
         section="## Row P: the process",
         tag="rt-process", sources=[CORE + "process.c"], test="process", unbuilt=TESTS + "unbuilt_process.c",
         spec=[(INC + "thread.h", "---- the process", "#endif /* RT_THREAD_H")] + TH_SPEC,
         shape_test=TESTS + "thread_run.c", shape_body=CORE + "thread_run.c",
         drop=[1, 2, 9, 10, 11, 12, 13, 14],
         note="Process exit is NOT part of this piece any more (it moved to `rt/crash.h` and `core/exit.c`, "
              "another worker's). Ignore every sentence below about `rt_process_exit`, `rt_process_terminate`, "
              "`rt_process_get_exit_code`, `_exit`, `status_of` and the fatal text of `GetExitCodeProcess`. This "
              "piece is the four functions of `core/process.c`: `rt_process_set_priority_class`, "
              "`rt_process_get_priority_class`, `rt_process_get_affinity`, `rt_process_set_affinity`. The plan's "
              "cases 1, 2 and 9 to 14 are removed below; the cases keep their numbers (3 to 8)."),
    dict(id="loader-pebuild", what="the PE writer for test images", handoff=LD, common=LD_COMMON,
         section="### Task 0: rt-loader-pebuild (haiku)",
         tag=None, sources=["runtime/tests/pe_build.c"], test="pe_build", unbuilt=None,
         spec=LD_PE + LD_IMAGE, shape_test=TESTS + "loader_image.c", shape_body="runtime/tests/loader_fixtures.c",
         note="The plan's case 11 says there is nothing to test: write it as "
              "`T_PLAN(11, \"misuse is not tested\") { T_TRUE(1); }`.", done="body"),
    dict(id="loader-export", what="export lookup", handoff=LD, common=LD_COMMON,
         section="### Task 1: rt-loader-export (haiku)",
         tag="rt-loader-export", sources=[CORE + "loader_export.c"], test="loader_export",
         unbuilt=TESTS + "unbuilt_loader_export.c",
         spec=[("fn", INC + "module.h", "rt_loader_export"), ("fn", INC + "module.h", "rt_module_export")] + LD_IMAGE,
         spec_test=LD_PE, shape_test=TESTS + "loader_image.c", shape_body=CORE + "loader_probe.c",
         needs_test=["core-loader-pebuild-body"], done="body"),
    dict(id="loader-query", what="module queries", handoff=LD, common=LD_COMMON,
         section="### Task 3: rt-loader-query (haiku)",
         tag="rt-loader-query", sources=[CORE + "loader_query.c"], test="loader_query",
         unbuilt=TESTS + "unbuilt_loader_query.c",
         spec=[(INC + "module.h", "/* The module whose HMODULE is `h`", "/* Export lookup on a lifted module"),
               (INC + "module.h", " * LAST ERROR.  Convention 1", " * A DLL FILE AND ITS LIFTED CODE"),
               (INC + "module.h", "---- the registry", " * LAST ERROR.  Convention 1")],
         spec_test=LD_PE, shape_test=TESTS + "loader_image.c", shape_body=CORE + "loader_probe.c",
         needs_test=["core-loader-pebuild-body"], done="body"),
    dict(id="loader-import", what="import binding", handoff=LD, common=LD_COMMON,
         section="### Task 2: rt-loader-import (haiku)",
         tag="rt-loader-import", sources=[CORE + "loader_import.c"], test="loader_import",
         unbuilt=TESTS + "unbuilt_loader_import.c",
         spec=[(CORE + "loader.h", "---- imports (loader_import.c)", "---- static TLS and threads"),
               ("docs/RUNTIME.md", "### 1.4 What goes in an import slot", "**Why.** A slot has to hold"),
               ("fn", INC + "module.h", "rt_loader_export")] + LD_IMAGE,
         spec_test=LD_PE, shape_test=TESTS + "loader_image.c", shape_body=CORE + "loader_image.c",
         needs_test=["core-loader-pebuild-body"], needs_body=["core-loader-export-body"], done="body"),
    dict(id="crash-excdecode", what="text for exception codes, records and contexts", handoff=CR, common=CR_COMMON,
         section=None, rows=["| D-test |", "| D-body |"], plan="### 5.1 `excdecode.c`",
         tag="rt-core-excdecode", sources=[CORE + "excdecode.c"], test="excdecode", unbuilt=None,
         spec=[(CORE + "report.h", "---- core/excdecode.c: text for exceptions", None), (INC + "win/exception.h", None, None)],
         common_body=CR_BODY, shape_test=TESTS + "crash_report.c", shape_body=CORE + "report.c"),
    # stub_pass: plan cases the planner accepts although they cannot fail against the stub.
    # Case 12 (an abort in progress beats an exit) ends in an abort with the stub too, because
    # the stub's own stop aborts the child; it does fail against a body that lets the exit win.
    dict(id="crash-exit", what="process exit", handoff=CR, common=CR_COMMON, stub_pass=(12,),
         section=None, rows=["| E-test |", "| E-body |"], plan="### 5.2 `exit.c`",
         tag="rt-core-exit", sources=[CORE + "exit.c"], test="exit", unbuilt=None,
         spec=[(INC + "crash.h", "---- process exit (section 9.4", "---- exceptions, plan A")],
         spec_test=[(INC + "test_child.h", None, None)],
         spec_body=[(CORE + "report.h", " * HOW A FATAL PATH IS WRITTEN", "---- core/report.c: the one-report rule")],
         common_body=CR_BODY, shape_test=TESTS + "crash_report.c", shape_body=CORE + "report.c"),
    dict(id="crash-exception", what="guest exceptions, plan A", handoff=CR, common=CR_COMMON,
         section=None, rows=["| X-test |", "| X-body |"], plan="### 5.3 `exception.c`",
         tag="rt-core-exception", sources=[CORE + "exception.c"], test="exception", unbuilt=None,
         spec=[(INC + "crash.h", "---- exceptions, plan A (section 9.3", "#endif /* RT_CRASH_H")],
         spec_test=[(INC + "test_child.h", None, None)], spec_body=[(CORE + "report.h", None, None)],
         common_body=CR_BODY, shape_test=TESTS + "crash_signal.c", shape_body=CORE + "trap.c",
         needs_body=["core-crash-excdecode-body"]),
    file_piece("io", "read, write, position", "---- read, write, position", "---- OVERLAPPED"),
    file_piece("attr", "attributes and times", "---- attributes and times", "---- names: delete, move, directories"),
    file_piece("dir", "delete, move, directories", "---- names: delete, move, directories", "---- find-file"),
    file_piece("find", "find-file", "---- find-file", "---- standard handles and the console"),
]

# ---- the window contract's pieces (docs/handoff/rt-window-core-1.md has their sections) -----------
# win-class, win-queue, win-window and gdi-dc were built by hand (units rt-window-core-1,
# rt-win-queue, rt-win-window) and have no row; the five pieces below were built from this table
# and are marked done after it.  gdi-object is the one window piece still to do: its section is in
# docs/handoff/rt-win-window.md.  win-keymap and the rest of win-frame are opus pieces.
WC = "docs/handoff/rt-window-core-1.md"
WH = INC + "window.h"
WC_COMMON = [(WC, "- Every test file of a window piece", "## Piece win-hook:")]
WC_SPEC = [(WH, " * NO SCREEN.  Nothing in this header draws", "#ifndef RT_WINDOW_H"),
           (WH, "---- opaque constants", "---- the desktop (core/win.c, built)"),
           (CORE + "win_priv.h", None, None)]
WC_NOTE = ("State that the piece's functions keep between calls is declared `static` in the piece's own source "
           "file and is read and written only with the win lock held (`rt_win_lock`, `rt_win_unlock`); that is "
           "allowed here although the rules say no new global state. A stop is `RT_WIN_STOP(\"<the public "
           "function>\", \"<what>, with the offending value\", ...)`, called after the lock is released. The "
           "section marks of `rt/window.h` still say `[not built]` for this piece: leave the header as it is.")


def win_piece(name, what, start, end, extra=()):
    return dict(id="win-" + name, what=what, handoff=WC, common=WC_COMMON, section="## Piece win-%s:" % name,
                tag="win-" + name, sources=[CORE + "win_%s.c" % name], test="win_" + name,
                unbuilt=TESTS + "unbuilt_win_%s.c" % name, spec=[(WH, start, end)] + list(extra) + WC_SPEC,
                shape_test=TESTS + "win_class.c", shape_body=CORE + "win_class.c", note=WC_NOTE,
                common_body=WC_COMMON)


PIECES += [
    win_piece("hook", "hook records", "---- hooks (core/win_hook.c)", "---- the clipboard (core/win_clipboard.c)"),
    win_piece("input", "cursor and keyboard state", "---- cursor and keyboard state (core/win_input.c)",
              "/* MapVirtualKeyExA [not built: win-keymap]"),
    win_piece("clipboard", "the clipboard of this process", "---- the clipboard (core/win_clipboard.c)",
              "#endif /* RT_WINDOW_H */"),
    win_piece("query", "what a window record is asked", "---- what a record is asked (core/win_query.c)",
              "---- show, paint state, focus, capture",
              extra=[("fn", WH, "rt_window_frame"), (WH, "/* What this machine's desktop is.", "/* GetSystemMetrics(index)")]),
    win_piece("state", "show, paint state, focus and capture of a window record",
              "---- show, paint state, focus, capture", "---- the message queue (core/win_queue.c)",
              extra=[("fn", WH, "rt_window_active")]),
]

GC = "docs/handoff/rt-win-window.md"
GH = INC + "gdi.h"
GC_COMMON = [(GC, "- Every test file of a gdi piece", "## Piece gdi-object:")]
PIECES += [
    dict(id="gdi-object", what="pens, brushes, fonts, bitmaps and regions as records", handoff=GC, common=GC_COMMON,
         section="## Piece gdi-object:", tag="gdi-object", sources=[CORE + "gdi_object.c"], test="gdi_object",
         unbuilt=TESTS + "unbuilt_gdi_object.c",
         spec=[(GH, "---- objects (core/gdi_object.c)", "#endif /* RT_GDI_H */"),
               (GH, " * NOTHING IS DRAWN.", "#ifndef RT_GDI_H"),
               (CORE + "win_priv.h", "/* ---- device contexts and GDI objects", "/* ---- the first show (core/win_window.c)")],
         shape_test=TESTS + "gdi_dc.c", shape_body=CORE + "gdi_dc.c", note=WC_NOTE.replace("rt/window.h", "rt/gdi.h"),
         common_body=GC_COMMON),
]

# Pieces whose two units are merged (swarm/units/core-<id>-test.json and -body.json say so): the
# stub and the unbuilt test are gone, so --check does not ask for them and no unit file is written.
for _p in PIECES:
    if _p["id"] in ("file-std", "thread-ctl", "thread-attach", "process", "crash-excdecode", "crash-exit",
                    "crash-exception", "file-io", "file-attr", "file-dir", "file-find",
                    "win-hook", "win-input", "win-clipboard", "win-query", "win-state"):
        _p["done"] = "body"


class Missing(Exception):
    pass


def read(rel):
    try:
        with open(os.path.join(ROOT, rel)) as f:
            return f.read().split("\n")
    except OSError:
        raise Missing("%s does not exist" % rel)


def rng(spec):
    """The text of one range (see the comment above TH_COMMON)."""
    if spec[0] == "fn":
        _, rel, name = spec
        lines = read(rel)
        decl = [i for i, l in enumerate(lines) if re.match(r"^[A-Za-z_].*\b%s\(.*\);" % re.escape(name), l)]
        if not decl:
            raise Missing("%s declares no function %s" % (rel, name))
        a = b = decl[0]
        while a > 0 and not lines[a].startswith("/*"):
            a -= 1
        return "\n".join(lines[a:b + 1])
    rel, start, end = spec
    lines = read(rel)
    a = 0
    if start is not None:
        hits = [i for i, l in enumerate(lines) if start in l]
        if not hits:
            raise Missing("%s has no line containing %r" % (rel, start))
        a = hits[0]
    b = len(lines)
    if end is not None:
        hits = [i for i in range(a + 1, len(lines)) if end in lines[i]]
        if not hits:
            raise Missing("%s has no line containing %r after %r" % (rel, end, start))
        b = hits[0]
    return "\n".join(lines[a:b]).strip("\n")


def section(rel, heading):
    """A handoff section: from the heading line to the next heading of the same or a higher level."""
    lines = read(rel)
    hits = [i for i, l in enumerate(lines) if l.startswith(heading)]
    if not hits:
        raise Missing("%s has no heading %r" % (rel, heading))
    level = len(heading) - len(heading.lstrip("#"))
    b = hits[0] + 1
    while b < len(lines) and not re.match(r"#{1,%d} " % level, lines[b]):
        b += 1
    return "\n".join(lines[hits[0] + 1:b]).strip("\n")


CASE_RE = re.compile(r"^(\d+)\. ", re.M)


def piece_text(p):
    """(brief text, test plan), both verbatim from the handoff, the plan without the dropped cases."""
    if p.get("plan"):
        lines = read(p["handoff"])
        after = p.get("rows_after", "| Task | Specification excerpt")
        head = [i for i, l in enumerate(lines) if l.startswith(after)]
        rows = [l for l in lines[head[0]:] if any(l.startswith(r) for r in p["rows"])] if head else []
        if len(rows) != len(p["rows"]):
            raise Missing("%s: expected the table rows %s" % (p["handoff"], p["rows"]))
        text = "\n".join(rows)
        if rows[0].startswith("|"):
            text = "| Task | Specification excerpt to paste | May call | Copy the shape from |\n|---|---|---|---|\n" + text
        plan = section(p["handoff"], p["plan"])
    else:
        whole = section(p["handoff"], p["section"])
        m = re.search(r"^Test plan", whole, re.M)
        if not m:
            raise Missing("%s, %s: no line starting 'Test plan'" % (p["handoff"], p["section"]))
        text, plan = whole[:m.start()].strip("\n"), whole[m.start():]
    drop = set(p.get("drop", []))
    plan = "\n".join(l for l in plan.split("\n") if not (CASE_RE.match(l) and int(CASE_RE.match(l).group(1)) in drop))
    return text, plan


def plan_cases(p):
    """The case numbers of the piece's plan, in order."""
    return [int(n) for n in CASE_RE.findall(piece_text(p)[1])]


def piece(pid):
    for p in PIECES:
        if p["id"] == pid:
            return p
    sys.exit("no piece %r; the pieces: %s" % (pid, ", ".join(p["id"] for p in PIECES)))


def fenced(specs):
    out = []
    for s in specs:
        rel = s[1] if s[0] == "fn" else s[0]
        lang = "c" if rel.endswith((".h", ".c")) else ""
        out.append("From `%s`:\n\n```%s\n%s\n```" % (rel, lang, rng(s)))
    return "\n\n".join(out)


def pasted(specs):
    return "\n\n".join(rng(s) for s in specs)


INTRO = """This repository recompiles a 64-bit Windows game for arm64 macOS. `runtime/` is our native
implementation of the Windows functions the game calls; `runtime/core/` holds the logic and
`runtime/include/rt/` its headers, whose comments are the specification. One piece of the core is
specified and not built: **{what}** ({functions}). Two workers build it. The first types the
piece's test from a numbered plan; the second writes the body and must make that test pass
without changing it."""

RULES = """- **Never guess.** The specification pasted below is complete for this piece. Where the text and
  your idea of Windows differ, the text wins. Every quoted string (a fatal text, a log line) is
  exact, byte for byte.
- Work only in this worktree, on the files named under "Files". Never read `game/` or `.env`. Do
  not read other files to "understand the project": what you need is on this page, plus the one
  file named under "Read". Do not run `tools/swarm/` scripts other than through the `make` lines here.
- Types: Windows types from `rt/wintypes.h` (`DWORD`, `BOOL`, `HANDLE`, `WCHAR`, `DWORD_PTR`) and
  `uint64_t` for a guest address; never `long`, `wchar_t` or `size_t` for guest data. Guest memory
  is read and written with `LD8/LD16/LD32/LD64(addr)` and `ST8/ST16/ST32/ST64(addr, value)`.
- Last error (convention 1 of `runtime/README.md`): a core function that fails sets the calling
  thread's last error itself and leaves it alone on success, unless its comment says otherwise.
- Names every piece meets: `rt_set_last_error(ctx, code)`; `rt_thread_set_last_error(code)` in a
  function that has no `ctx`; `return FAIL(ctx, code, value);` sets the error and returns the
  value; `rt_fatal("printf format", ...)` prints a fatal report and never returns;
  `rt_galloc(size)` returns zeroed guest-visible memory as `void *` and is fatal when memory is
  exhausted. A guest address IS the host pointer value: `(uint64_t)(uintptr_t)p` and back.
- While a lock is held do only what its paragraph allows: unlock first, then call `rt_fatal`,
  log or set the last error.
- The build is `-Wall -Wextra -Werror`: no unused variable, parameter, function or label; cast
  where signedness differs; `(void)x;` for a parameter you do not use.
- **Write a long file in parts.** A reply is cut off at 32,000 tokens and the run is lost with it:
  create the file with its includes, helpers and first `TEST` or function (under about 150
  lines), then add the rest with one edit per `TEST` or function. The same for a long edit.
- Pipe every build through `tail`: `make rt-test 2>&1 | tail -15`. The machine is busy; a first
  build takes minutes."""

TEST_BRIEF = """# Unit core-{id}-test: the test of {what}

{intro}

**You are the first worker.** You write one file, the test, typed from the plan below. The
functions are stubs today (each stops the process with a "FATAL unimplemented runtime core
function" report), so your test cannot pass yet and is not part of the build: it goes into
`runtime/tests/core/pending/`. The plan is the contract between you and the second worker: type
every case as it is written, with the exact values; do not weaken, skip, merge or invent cases.

## Files

- create `{pending}`

Nothing else: no other file is created, edited or deleted, and nothing under `swarm/`.

## How the file is written (the check enforces every line of this)

- It will be moved, unchanged, to `runtime/tests/core/{test}.c`, and is compiled as if it were
  there already. So a relative include is written from that directory: `#include "wait_util.c"`,
  `#include "../../core/thread_priv.h"`. It includes `"rt/test.h"`; take the other includes and
  the signatures of the helpers the plan names from the file under "Read".
- It holds one or more tests. Each is `TEST(core, {test})` or `TEST(core, {test}_<word>)`
  followed by a `{{ ... }}` body; every TEST runs in its own process, started from scratch. Start
  a new TEST where the plan says "fresh process" or where a case needs a state the earlier cases
  have spoiled.
- **Every numbered case N of the plan is exactly one block** `T_PLAN(N, "a few words") {{ ... }}`
  directly in the body of a TEST, with N the plan's own number written in decimal. Every number
  of the plan appears exactly once in the file: {cases}. Everything the case does and checks is
  inside its block.
- No function of this piece is called outside a `T_PLAN` block (a helper that is called only
  from inside blocks may call them). Variables that a later case of the same TEST needs are
  declared before the first block; before the first block there are only declarations.
- Against today's stubs every case must either stop in the stub or fail a check. A case that
  passes today checks nothing of the piece, and the check refuses it.
- Where the plan leaves a value open (what a thread returns, which index the main thread holds),
  choose one and check it. "LE unchanged" and "last error X" need the sentinel planted just
  before the call. A check of a precondition that makes a later `== 0` meaningful may be added
  inside the case; nothing else is added.
- `T_` macros are used only on the main thread (the one that runs the TEST body): a worker
  thread records what it saw in statics and the main thread checks them after the join.
{note}
## The harness (`rt/test.h`)

    TEST(core, name) {{ ... }}            one test; its own process
    T_PLAN(3, "words") {{ ... }}          case 3 of the plan
    T_TRUE(cond);  T_EQ(a, b);            checks; the test goes on after a failure. T_EQ compares
                                         as 64-bit values: write (DWORD)-1, not -1
    T_REQUIRE(cond);                     a check that leaves the TEST if it fails
    T_LAST_ERROR()                       the main thread's last error
    T_TRAPS(stmt, "text");               stmt must end in a fatal report containing "text"; it
                                         runs in a forked child, so it changes nothing here
    T_LOGGED(RT_CH_X, "text")            true if a line containing "text" was logged on the channel
    T_CTX()                              the main guest thread's Ctx *
    GUEST_FN(name) {{ ...; GUEST_RET(ctx); }}   a guest function written in C; arguments in
                                         ctx->r[1] (rcx), r[2] (rdx), r[8], r[9]; result in ctx->r[0];
                                         T_GUEST_ADDR(name) is its guest address

## Read (one file, for the shape; do not read others)

`{shape}`

## What every test of this contract follows (from `{handoff}`, verbatim)

{common}

## The piece: brief text and test plan (from `{handoff}`, verbatim)

{text}

{plan}

## The specification the plan tests

{spec}

## Rules

{rules}

## Done

This exits 0 on a clean, committed worktree (commit first: `git add {pending}` and
`git commit -m "Core test: {what}"`):

    make core-test-check PIECE={id} 2>&1 | tail -40

It checks that the branch adds exactly that one file; that the file compiles with no warning as
the suite compiles a test; that every case number of the plan is one `T_PLAN` block; that each
case, run alone against today's stub, starts and does not pass; and that `make rt-test` still
passes. Read what it prints, fix the file, commit, run it again.
"""

BODY_BRIEF = """# Unit core-{id}-body: the body of {what}

{intro}

**You are the second worker.** The test exists: `{pending}`, typed from the plan below by
another worker. You write the body in {sources}, move the test into the build and make
`make rt-test` pass.

## Files

{file_list}

Nothing else is created, edited or deleted. **The test is moved byte for byte**: do not edit
it, not even its comments or spacing; the check compares it with the file you were given. If you
are sure that a check of the test contradicts the specification below, do not bend the body to
it: build what the specification says, commit, and end your reply with a line
`TEST-WRONG: case N: <one sentence>`; the unit's check will fail and a reviewer decides.

## The file you fill in, as it is now

Its top comment says what state it has and what it may call. Keep the comment (reword "not built
yet"), keep the signatures, replace every `RT_CORE_UNIMPLEMENTED`{keeps}.

{stub}

## Read (one file, for the shape; do not read others)

`{shape}`

## The piece: brief text and test plan (from `{handoff}`, verbatim)

{text}
{common_body}
The plan your body must pass (it is what `{pending}` checks, case by case as
`T_PLAN(N, ...)` blocks; read that file when a case fails):

{plan}

## The specification

{spec}

## Rules

{rules}
- Keep the file under about 300 lines. No new global state beyond what the stub declares; no new
  header; no change to any header.

## Done

This exits 0 on a clean, committed worktree:

    make core-body-check PIECE={id} 2>&1 | tail -40

It checks that the branch changes only the files listed above; that `runtime/tests/core/{test}.c`
is byte for byte the pending file; that no `RT_CORE_UNIMPLEMENTED` is left{keeps_done}; that
`make rt-test` passes; and that every case of the plan ran, made a check and failed none. While
you work, run only your tests: `make rt-test SHIM='{names}' 2>&1 | tail -30`
(a TEST is named `core/<name>`; `build/runtime/rt-test --list | grep {test}` shows yours).
"""


def functions_of(p):
    """The names the handoff's task table gives for the piece's source file."""
    for l in read(p["handoff"]):
        if l.startswith("|") and ("`%s`" % p["sources"][0].replace("runtime/", "") in l or "`%s`" % p["sources"][0] in l):
            names = re.findall(r"`(_?rt_[a-z0-9_/*]+|_[a-z0-9_/*]+)`", l)
            if names:
                return names
    return []


def build(p, kind):
    text, plan = piece_text(p)
    fns = [f for f in functions_of(p) if "exit" not in f or p["id"] != "process"]
    pending = "runtime/tests/core/pending/%s.c" % p["test"]
    intro = INTRO.format(what=p["what"], functions=", ".join("`%s`" % f for f in fns) or "see below")
    note = "- For this piece: %s\n" % p["note"] if p.get("note") else ""
    spec = p["spec"] + p.get("spec_" + kind, [])
    common = dict(id=p["id"], what=p["what"], intro=intro, pending=pending, test=p["test"], handoff=p["handoff"],
                  text=text, plan=plan, spec=fenced(spec), rules=RULES)
    if kind == "test":
        brief = TEST_BRIEF.format(cases=", ".join(str(n) for n in plan_cases(p)), note=note, shape=p["shape_test"],
                                  common=pasted(p["common"]), **common)
        needs = ["rt-core-tasks"] + p.get("needs_test", [])
    else:
        files = ["- write the body in `%s`%s" % (s, "" if os.path.exists(os.path.join(ROOT, s)) else " (a new file)")
                 for s in p["sources"]]
        files.append("- `git mv %s runtime/tests/core/%s.c` (the content unchanged)" % (pending, p["test"]))
        if p.get("unbuilt"):
            files.append("- `git rm %s` (it proves the stub stops; with a body it would fail)" % p["unbuilt"])
        files += ["- you may also change `%s`" % a for a in p.get("also", [])]
        stubs = [s for s in p["sources"] if os.path.exists(os.path.join(ROOT, s))]
        stub = fenced([(s, None, None) for s in stubs]) if stubs else "(the file does not exist yet: you create it)"
        cb = "\n" + pasted(p["common_body"]) + "\n" if p.get("common_body") else ""
        if note:
            cb = "\nFor this piece: %s\n%s" % (p["note"], cb)
        brief = BODY_BRIEF.format(sources=" and ".join("`%s`" % s for s in p["sources"]), file_list="\n".join(files),
                                  stub=stub, shape=p["shape_body"], common_body=cb,
                                  keeps=" you were not told to keep" if p.get("keeps_stub") else "",
                                  keeps_done=" except where the brief says one stays" if p.get("keeps_stub") else "",
                                  names="core/%s" % p["test"], **common)
        needs = ["rt-core-tasks", "core-%s-test" % p["id"]] + p.get("needs_body", [])
    uid = "core-%s-%s" % (p["id"], kind)
    unit = {"id": uid, "title": "Core %s: %s (%s)" % (kind, p["what"], p["test"]), "kind": "core-" + kind,
            "issue": 24, "model": "haiku", "priority": 10 + PIECES.index(p), "needs": needs,
            "check": ["make core-%s-check PIECE=%s" % (kind, p["id"])], "check_timeout": 3600, "state": "todo"}
    return uid, brief, unit


def done(p, kind):
    return p.get("done") == "body" or p.get("done") == kind


def check():
    problems, owner = [], {}
    for p in PIECES:
        try:
            cases = plan_cases(p)
            if not cases or len(set(cases)) != len(cases):
                problems.append("%s: the plan has no numbered cases, or a number twice: %s" % (p["id"], cases))
            for kind in ("test", "body"):
                build(p, kind)
        except Missing as e:
            problems.append("%s: %s" % (p["id"], e))
        mine = p["sources"] + ["runtime/tests/core/%s.c" % p["test"]] + ([p["unbuilt"]] if p.get("unbuilt") else [])
        for f in mine:
            if f in owner:
                problems.append("%s is named by two pieces: %s and %s" % (f, owner[f], p["id"]))
            owner[f] = p["id"]
        state = p.get("done")
        for f in p["sources"] + [p["shape_test"], p["shape_body"]]:
            if not os.path.exists(os.path.join(ROOT, f)) and not (p["tag"] is None and f in p["sources"]):
                problems.append("%s: %s does not exist" % (p["id"], f))
        if p.get("unbuilt") and state != "body" and not os.path.exists(os.path.join(ROOT, p["unbuilt"])):
            problems.append("%s: %s does not exist" % (p["id"], p["unbuilt"]))
        if p["tag"] and state != "body" and not any(
                'RT_CORE_UNIMPLEMENTED("%s")' % p["tag"] in "\n".join(read(s)) for s in p["sources"]):
            problems.append("%s: no RT_CORE_UNIMPLEMENTED(\"%s\") in %s" % (p["id"], p["tag"], p["sources"]))
        ids = ["core-%s-%s" % (q["id"], k) for q in PIECES for k in ("test", "body")] + ["rt-core-tasks"]
        for n in p.get("needs_test", []) + p.get("needs_body", []):
            if n not in ids:
                problems.append("%s: needs %s, which is no unit of this table" % (p["id"], n))
    for x in problems:
        print("PROBLEM  " + x)
    print("%d pieces, %d units: %s" % (len(PIECES), 2 * len(PIECES), "ok" if not problems else "%d problems" % len(problems)))
    return 1 if problems else 0


def main():
    args = sys.argv[1:]
    if args == ["--check"]:
        sys.exit(check())
    if args == ["--list"]:
        for p in PIECES:
            for kind in ("test", "body"):
                uid, brief, unit = build(p, kind)
                print("%-28s %-5s %6d tokens  needs %s" % (uid, "done" if done(p, kind) else "todo", len(brief) // 4,
                                                          ", ".join(unit["needs"][1:]) or "-"))
        return
    if len(args) == 2 and args[0] == "--show":
        for p in PIECES:
            for kind in ("test", "body"):
                uid, brief, _ = build(p, kind)
                if args[1] == uid:
                    print(brief)
                    return
        sys.exit("no unit %r (--list shows them)" % args[1])
    if args and not (len(args) == 2 and args[0] == "--out"):
        sys.exit(__doc__)
    out = os.path.abspath(args[1]) if args else os.path.join(ROOT, "swarm")
    os.makedirs(os.path.join(out, "briefs"), exist_ok=True)
    os.makedirs(os.path.join(out, "units"), exist_ok=True)
    for p in PIECES:
        for kind in ("test", "body"):
            uid, brief, unit = build(p, kind)
            with open(os.path.join(out, "briefs", uid + ".md"), "w") as f:
                f.write(brief)
            path = os.path.join(out, "units", uid + ".json")
            if os.path.exists(path) or done(p, kind):
                continue
            with open(path, "w") as f:
                json.dump(unit, f, indent=2)
                f.write("\n")
            print("wrote %s (%d tokens of brief, roughly)" % (uid, len(brief) // 4))


if __name__ == "__main__":
    main()
