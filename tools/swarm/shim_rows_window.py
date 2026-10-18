#!/usr/bin/env python3
"""The rows of make_shim_tasks.py for user32, gdi32 and imm32, and the table of the window
contract (unit rt-contract-window; docs/handoff/rt-contract-window.md).

make_shim_tasks.py appends WINDOW_TASKS to its TASKS; nothing else of that file knows about
this one, so rows added there and rows added here do not meet in a merge.

    .venv/bin/python tools/swarm/shim_rows_window.py            the contract's table, as Markdown
    .venv/bin/python tools/swarm/shim_rows_window.py --summary  the counts
    .venv/bin/python tools/swarm/shim_rows_window.py --importers  the IMPORTERS block, regenerated
    .venv/bin/python tools/swarm/shim_rows_window.py --check    every one of the 167 imports is
                                                                answered by exactly one of: a row,
                                                                BUILT, STOPS; exit 1 and why if not

Every function the boot-path DLLs import from the three libraries is in exactly one place:
a row of WINDOW_TASKS (the contract gives it an answer; `says` is the answer in one line),
BUILT (written by the contract unit itself), or STOPS (it stays a stop, with the reason).
IMPORTERS is generated from inventory/inventory.json (it names who imports what; the order
of the rows follows it) and is not edited by hand.

A row's `waits` names the ledger units that will build the core piece it sits on.  They do
not exist yet: while one has no file under swarm/units/, make_shim_tasks.py holds the row
back exactly as `hold` does, and the day the file exists the name counts as a `need`.

Everything below about Windows was written FROM MEMORY (prototypes included).  Two reviews then
checked part of it (docs/reviews/contracts-check.md and contracts-check-2.md); their wrong
statements, C1 to C9 and D1 to D21, are corrected in the rows that repeated them, and a row
says "Wine <test file>" or "review <row>" where a statement is checked (a row N.. or S.. of a
review is one that could NOT be settled).  Everything else is still from memory.
"""

import os
import sys

# The units that will build the core pieces (see the handoff, "Core pieces").
CLASS, WINDOW, QUERY, STATE = "core-win-class-body", "rt-win-window", "core-win-query-body", "core-win-state-body"
QUEUE, INPUT, KEYMAP, HOOK = "rt-win-queue", "core-win-input-body", "rt-win-keymap", "core-win-hook-body"
# gdi-dc was built by rt-win-window itself (no core-gdi-dc-body unit will ever exist).
CLIP, GDIDC, GDIOBJ = "core-win-clipboard-body", "rt-win-window", "core-gdi-object-body"

G_WIN, G_MSG, G_DESK, G_GDI = "classes and windows", "messages and the queue", "metrics and monitors", \
    "device contexts, fonts and bitmaps"
G_CUR, G_KEY, G_CLIP, G_IME, G_REST = "cursors and icons", "keyboard, mouse and raw input", "clipboard", \
    "the input method", "the rest"
GROUPS = [G_WIN, G_MSG, G_DESK, G_GDI, G_CUR, G_KEY, G_CLIP, G_IME, G_REST]

WIN_ERR = """\
- The core functions of `rt/window.h` and `rt/gdi.h` set last error themselves when they fail
  and leave it alone on success. The shim returns the result and sets nothing; the test checks
  every code the core's comment names.
- Where the core's comment says a case STOPS, the shim adds no guard for it. List each such case
  that a test can reach in `RT-TRAPS` as `core: <case>` (the status is then `partial`) and prove
  each listed one with a `T_TRAPS(..., "<core function>: not implemented")`; name the ones you
  left out in your reply.
"""
WIN_TEST = """\
- A test that needs a window makes it through the core, never through another shim (include
  `rt/window.h`):
      GUEST_FN(proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }  /* rdx: the message */
      WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
      rt_wndclass_register(&wc, true);
      RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                           .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
      HWND h = rt_window_create(T_CTX(), &a);
  Handles that are not windows, for the failure cases: `NULL`, `(HWND)0x40000FF0` (a free slot),
  and a window after `rt_window_destroy(T_CTX(), h)`.
"""
FIXED = """\
- An `RT-TRAPS` entry must be a piece of the text of the `RT_UNIMPLEMENTED` call that makes the
  stop (`make shim-check` matches them): keep the entry short and put it at the start of the
  message. A constant a note names with its number, and that `rt/win/` does not define, is
  written as the number: that is not a missing piece.
- Status and answer are the contract's (`docs/handoff/rt-contract-window.md`): write exactly
  this answer. Everything said about Windows here is from memory; do not "correct" it from
  your own memory either: if the documentation page you can open contradicts a note, implement
  the note and say so in your reply.
"""
ABSENT_IME = FIXED + """\
- Status `absent`, signed off in the contract: this machine has no input method (the keyboard
  layout is US English, `docs/RUNTIME.md` 8.4 and 9.2), so no input context exists and no
  `HIMC` was ever handed out: every `HIMC` a caller passes is NULL or invalid. The two are NOT
  answered alike (Wine `imm32/tests/imm32.c`, `test_InvalidIMC` and `test_ImmNotifyIME`; review
  C6, V18). For a function that takes an `HIMC`: **a non-NULL `HIMC`** gets the function's
  failure value with last error `ERROR_INVALID_HANDLE` (`return FAIL(ctx, ERROR_INVALID_HANDLE,
  <failure value>);`); **a NULL `HIMC`** gets the failure value with last error left alone,
  unless the function's own note below says otherwise. No other argument is looked at except
  where a note says so, and nothing is written through any pointer. `RT-TRAPS` is `none`
  unless a note names one.
- An `absent` shim logs before it answers: `rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG,
  "imm32.dll!<Name>", "<Name>: there is no input method");`. The test checks the line with
  `T_LOGGED(RT_CH_WINDOW, "<Name>: there is no input method")`, the return value, last error
  after `SetLastError(0xdeadbeef)` for a NULL `HIMC` and for `(HIMC)0xdeadcafe` (6,
  `ERROR_INVALID_HANDLE`), and that a buffer preset to 0xA5 keeps its bytes.
"""


def row(id, group, members, protos, says, notes, lib="user32", waits=(), header=None, core=(),
        model="GetTickCount", model_lib="kernel32", **more):
    return dict(id="win-" + id, lib=lib, members=members, needs=["rt-contract-window"], waits=list(waits),
                header=header, core=list(core), model=model, model_lib=model_lib, protos=protos,
                group=group, says=says, notes=notes, issue=52, **more)


WH, GH = "rt/window.h", "rt/gdi.h"
METRICS = dict(model="GetSystemMetrics", model_lib="user32")

WINDOW_TASKS = [
    # ---- classes and windows ----------------------------------------------------------------------
    row("register-class-ex", G_WIN, ["RegisterClassExA", "RegisterClassExW"],
        ["ATOM RegisterClassExA(const WNDCLASSEXA *lpwcx)", "ATOM RegisterClassExW(const WNDCLASSEXW *lpwcx)"],
        "a class record; the atom", WIN_ERR + """\
- One core call each: `return rt_wndclass_register((const WNDCLASSEXW *)lpwcx, false);` for A
  (the two structures have one layout) and `(lpwcx, true)` for W.
- Tests: an atom of 0xC000 or above; the same name twice (`ERROR_CLASS_ALREADY_EXISTS`); `cbSize`
  72; a NULL name; `cbWndExtra` -1; an A class found again by the W function under the same
  name in other case; a name registered with `hInstance` `(HINSTANCE)(uintptr_t)rt_module_exe()->base`
  (`rt/module.h`) and then with `hInstance` NULL fails with `ERROR_CLASS_ALREADY_EXISTS`: NULL is
  the main module (Wine `user32/tests/class.c`, `test_instances`; review D6); last error left
  alone on success; the stops for an atom as name, a NULL procedure, a menu name and
  `cbWndExtra` 41 (Windows refuses more than 40 extra bytes, review V57, with a code that is
  not settled, S2: so it is a stop).""",
        header=WH, core=["rt_wndclass_register"]),
    row("register-class-a", G_WIN, ["RegisterClassA"], ["ATOM RegisterClassA(const WNDCLASSA *lpWndClass)"],
        "a class record; the atom", WIN_ERR + """\
- `WNDCLASSA` is `WNDCLASSEXA` without `cbSize` and `hIconSm`. A NULL pointer goes to the core as
  NULL. Otherwise build a `WNDCLASSEXA` on the stack (`cbSize = 80`, `hIconSm = NULL`, the ten
  other members copied) and `return rt_wndclass_register((const WNDCLASSEXW *)&ex, false);`.
- Tests: an atom of 0xC000 or above; the same name twice (`ERROR_CLASS_ALREADY_EXISTS`); a NULL
  structure, a NULL name and `cbWndExtra` -1 (`ERROR_INVALID_PARAMETER`);
  `rt_wndclass_unregister(RT_TEXT_A(name), NULL)` then succeeds, which proves the name arrived;
  the stops for an atom as name, a NULL procedure, a menu name and 41 extra bytes.""",
        header=WH, core=["rt_wndclass_register", "rt_wndclass_unregister"]),
    row("unregister-class", G_WIN, ["UnregisterClassA", "UnregisterClassW"],
        ["BOOL UnregisterClassA(LPCVOID lpClassName, HINSTANCE hInstance)",
         "BOOL UnregisterClassW(LPCVOID lpClassName, HINSTANCE hInstance)"],
        "the class record is removed; fails while a window of the class exists", WIN_ERR + """\
- The name is `LPCVOID` in the `SHIM` line because it may be an atom, which the call trace must
  not print as text. `return rt_wndclass_unregister(RT_TEXT_A((const char *)lpClassName), hInstance);`
  and `RT_TEXT_W((const WCHAR *)lpClassName)`.
- Tests: register through the core, unregister by name, by name in other case and by atom
  (`(LPCVOID)(uintptr_t)atom`); twice (`ERROR_CLASS_DOES_NOT_EXIST`); a NULL name.""",
        header=WH, core=["rt_wndclass_unregister", "rt_wndclass_register"]),
    row("create-window", G_WIN, ["CreateWindowExA", "CreateWindowExW"],
        ["HWND CreateWindowExA(DWORD dwExStyle, LPCVOID lpClassName, LPCSTR lpWindowName, DWORD dwStyle, INT X, "
         "INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)",
         "HWND CreateWindowExW(DWORD dwExStyle, LPCVOID lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, INT X, "
         "INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)"],
        "a window record, after WM_NCCREATE, WM_NCCALCSIZE and WM_CREATE, then WM_SIZE and WM_MOVE for a "
        "popup or a child; stops for a visible window and, until win-frame, for an overlapped one", WIN_ERR + """\
- Fill an `RtWindowCreate` from the twelve arguments (`cls` and `title` as `RT_TEXT_A` or
  `RT_TEXT_W`, `unicode` false or true) and `return rt_window_create(ctx, &args);`. The class
  name is `LPCVOID` because it may be an atom.
- Tests, with a class registered through the core and a guest procedure that records the
  messages it gets (their numbers into a static array) and returns 1 for `WM_NCCREATE`: a
  `WS_POPUP` window gives a handle and exactly the five messages 0x81, 0x83, 0x01, 0x05, 0x03
  in that order (`WM_NCCREATE`, `WM_NCCALCSIZE`, `WM_CREATE`, `WM_SIZE`, `WM_MOVE`: Wine
  `user32/tests/msg.c`, `WmCreateChildPopupSeq`, and the first five sent messages of
  `WmCreatePopupSeq`; review census-rows H1: `WmCreateInvisiblePopupSeq`, cited here before, is
  the sequence of a popup whose `WM_CREATE` handler sets `WS_VISIBLE`, and has two style
  messages more); a `WS_CHILD` of it records the same five
  and its parent then gets 0x210 (`WM_PARENTNOTIFY`) with the low word of wParam 1
  (`WM_CREATE`); a `WS_CHILD` window with a NULL parent gives NULL with last error 1406
  (`ERROR_TLW_WITH_WSCHILD`) and the procedure is not called; an unknown class
  (`ERROR_CANNOT_FIND_WND_CLASS`); a parent that is not a window; the stop for a
  message-only window (`HWND_MESSAGE`: which messages its creation sends is not settled);
  `GWL_STYLE` of a `WS_POPUP` window reads 0x84000000, with `WS_CLIPSIBLINGS`
  (`docs/reviews/win-window-sequences.md` row 1.10); a `WS_POPUP` window with x = y = 10 and
  cx = cy = -10 has the window rectangle {10, 10, 10, 10} (`test_CreateWindow`; review D8); two
  children of one parent are in creation order (`rt_window_get(parent, GW_CHILD)` is the
  first; review D2); the stop for a procedure that returns 0 for `WM_NCCREATE` (Windows
  returns NULL, but what it sends to the window first is not settled: row 1.13); the stop for
  `WS_VISIBLE` on a popup; the stop for `CreateWindowExA` of a class registered with
  `rt_wndclass_register(&wc, true)` (the text would have to be converted: row 1.9); the stop for style 0 and for `WS_POPUP | WS_THICKFRAME`
  (`WM_GETMINMAXINFO` needs the frame sizes of the piece win-frame).""",
        waits=[WINDOW], header=WH, core=["rt_window_create", "rt_wndclass_register"]),
    row("destroy-window", G_WIN, ["DestroyWindow"], ["BOOL DestroyWindow(HWND hWnd)"],
        "WM_DESTROY to the window and down its children, WM_NCDESTROY back up, then the records are gone",
        WIN_ERR + WIN_TEST + """\
- `return rt_window_destroy(ctx, hWnd);`.
- Tests: TRUE and the handle is invalid afterwards (a second call fails with
  `ERROR_INVALID_WINDOW_HANDLE`); NULL. The order (Wine `user32/tests/msg.c`,
  `test_DestroyWindow`; review D1), with one procedure that records (message, window) for a
  parent P, its child C and C's child G: `DestroyWindow(P)` records `WM_DESTROY` (2) for P, C,
  G and then `WM_NCDESTROY` (0x82) for G, C, P, with last error left alone, and all three
  handles are invalid; with a fresh P and C, `DestroyWindow(C)` gives P a `WM_PARENTNOTIFY`
  (0x210) whose low word of wParam is 2 before C gets `WM_DESTROY`; a window owned by P gets
  both its messages before P gets `WM_DESTROY`.""",
        waits=[WINDOW], header=WH, core=["rt_window_destroy"]),
    row("def-window-proc", G_WIN, ["DefWindowProcA", "DefWindowProcW"],
        ["LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)",
         "LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)"],
        "the documented default for the messages a record can get; any other system message stops",
        WIN_ERR + WIN_TEST + """\
- `return rt_window_def_proc(ctx, hWnd, Msg, wParam, lParam, false);` and `true` for W.
- Tests: `WM_NULL`, `WM_USER + 1`, 0xC123 give 0; `WM_PARENTNOTIFY` (0x210) gives 0, no stop
  (review D1); `WM_SETTEXT` then `WM_GETTEXTLENGTH` and `WM_GETTEXT`; `WM_WINDOWPOSCHANGED`
  (0x47) with a `WINDOWPOS` whose flags are `SWP_NOCLIENTMOVE` (0x1000) makes the window's own
  procedure get `WM_SIZE` and no `WM_MOVE`, with `SWP_NOCLIENTSIZE` (0x0800) `WM_MOVE` and no
  `WM_SIZE`, with neither `WM_MOVE` then `WM_SIZE`, with both nothing (Wine
  `user32/tests/msg.c`, `WmSWP_ResizeSeq`, `WmSWP_MoveSeq`; review D7); an invalid window gives
  0; the stop for message 0x0200 (`WM_MOUSEMOVE`).""",
        waits=[WINDOW], header=WH, core=["rt_window_def_proc"]),
    row("call-window-proc", G_WIN, ["CallWindowProcA", "CallWindowProcW"],
        ["LRESULT CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)",
         "LRESULT CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)"],
        "calls the guest procedure and returns its result", WIN_ERR + """\
- `return rt_window_call_proc(ctx, lpPrevWndFunc, hWnd, Msg, wParam, lParam, false);` and `true`.
- Tests: a `GUEST_FN` that returns `rcx + rdx + r8 + r9` (`ctx->r[1] + (UINT)ctx->r[2] + ctx->r[8] +
  ctx->r[9]`) proves the four arguments arrive in order and the 64-bit result comes back; the
  stop for a procedure of 0.""",
        waits=[WINDOW], header=WH, core=["rt_window_call_proc"]),
    row("window-long", G_WIN, ["GetWindowLongA", "SetWindowLongA"],
        ["LONG GetWindowLongA(HWND hWnd, INT nIndex)", "LONG SetWindowLongA(HWND hWnd, INT nIndex, LONG dwNewLong)"],
        "the record's styles, identifier, user data and extra bytes, 32 bits", WIN_ERR + WIN_TEST + """\
- `return (LONG)rt_window_get_long(hWnd, nIndex, 4, false);` and
  `return (LONG)rt_window_set_long(hWnd, nIndex, dwNewLong, 4, false);`.
- Tests: `GWL_STYLE` is the style given at creation; `GWLP_USERDATA` set then got; extra bytes at
  offsets 0 and 12 of 16 (and offset 13: `ERROR_INVALID_INDEX`); index -99; an invalid window;
  a set returns the previous value. The three pointer indexes are refused on 64-bit Windows
  (Wine `user32/tests/win.c`, `test_set_window_long_size`): after `SetLastError(0xdeadbeef)`,
  `GetWindowLongA(h, -4)` is 0 with last error 1413 (`ERROR_INVALID_INDEX`);
  `SetWindowLongA(h, -4, 0x12345678)` is 0 with 1413 and `rt_window_get_long(h, -4, 8, true)`
  is still the class's procedure; the same for -6 (`GWLP_HINSTANCE`) and -8
  (`GWLP_HWNDPARENT`). `GWLP_USERDATA` set to `1 << 32 | 123` through
  `rt_window_set_long(h, GWLP_USERDATA, v, 8, true)` reads 123 here.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_get_long", "rt_window_set_long"]),
    row("window-long-ptr-a", G_WIN, ["GetWindowLongPtrA", "SetWindowLongPtrA"],
        ["LONG_PTR GetWindowLongPtrA(HWND hWnd, INT nIndex)",
         "LONG_PTR SetWindowLongPtrA(HWND hWnd, INT nIndex, LONG_PTR dwNewLong)"],
        "the same, 64 bits", WIN_ERR + WIN_TEST + """\
- `return rt_window_get_long(hWnd, nIndex, 8, false);` and `rt_window_set_long(hWnd, nIndex,
  dwNewLong, 8, false)`.
- Tests: a 64-bit value in `GWLP_USERDATA` and in the extra bytes at offset 8 survives whole;
  offset 9 of 16 fails; `GWLP_WNDPROC` on a W window is the core's stop.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_get_long", "rt_window_set_long"]),
    row("window-long-ptr-w", G_WIN, ["GetWindowLongPtrW", "SetWindowLongPtrW"],
        ["LONG_PTR GetWindowLongPtrW(HWND hWnd, INT nIndex)",
         "LONG_PTR SetWindowLongPtrW(HWND hWnd, INT nIndex, LONG_PTR dwNewLong)"],
        "the same, 64 bits; GWLP_WNDPROC replaces the procedure", WIN_ERR + WIN_TEST + """\
- As the A pair with `unicode` true.
- Tests: as the A pair; `GWLP_WNDPROC` returns the procedure the class was registered with and
  a set replaces it (get again).""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_get_long", "rt_window_set_long"]),
    row("class-long", G_WIN, ["GetClassLongPtrA"], ["ULONG_PTR GetClassLongPtrA(HWND hWnd, INT nIndex)"],
        "what the window's class was registered with", WIN_ERR + WIN_TEST + """\
- `return rt_wndclass_get_long(hWnd, nIndex, false);`.
- Tests: `GCL_CBWNDEXTRA` is 16; `GCW_ATOM` is the atom the registration returned;
  `GCLP_HCURSOR` as registered; index -99 (`ERROR_INVALID_INDEX`); an invalid window.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_wndclass_get_long"]),
    row("window-rect", G_WIN, ["GetWindowRect", "GetClientRect"],
        ["BOOL GetWindowRect(HWND hWnd, LPRECT lpRect)", "BOOL GetClientRect(HWND hWnd, LPRECT lpRect)"],
        "the record's rectangle; the monitor's for the desktop window", WIN_ERR + WIN_TEST + """\
- `return rt_window_get_rect(hWnd, lpRect);` and `rt_window_get_client_rect`.
- Tests: the window of the recipe gives {10, 20, 310, 220} and {0, 0, 300, 200};
  `RT_HWND_DESKTOP` gives {0, 0, 1920, 1080} in both; an invalid window leaves a preset RECT
  untouched; the stop for a NULL rectangle (what Windows does there is not settled, review S13).""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_get_rect", "rt_window_get_client_rect"]),
    row("client-screen", G_WIN, ["ClientToScreen", "ScreenToClient"],
        ["BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint)", "BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint)"],
        "the point moved by the record's position", WIN_ERR + WIN_TEST + """\
- `return rt_window_client_to_screen(hWnd, lpPoint);` and `rt_window_screen_to_client`.
- Tests: {5, 6} becomes {15, 26} and back; the desktop window changes nothing; an invalid
  window leaves the point untouched.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_client_to_screen", "rt_window_screen_to_client"]),
    row("window-text-a", G_WIN, ["GetWindowTextA", "SetWindowTextA"],
        ["INT GetWindowTextA(HWND hWnd, LPSTR lpString, INT nMaxCount)",
         "BOOL SetWindowTextA(HWND hWnd, LPCSTR lpString)"],
        "the record's title, through WM_GETTEXT and WM_SETTEXT", WIN_ERR + """\
- `return rt_window_get_text(ctx, hWnd, lpString, nMaxCount, false);` and
  `return rt_window_set_text(ctx, hWnd, RT_TEXT_A(lpString));`.
- Tests need a window whose procedure answers the two text messages: register the class with
  the guest address of a `GUEST_FN` that keeps a text itself (copy on `WM_SETTEXT`, 0x0C, from
  `ctx->r[9]`; on `WM_GETTEXT`, 0x0D, copy at most `ctx->r[8]` bytes to `ctx->r[9]` and return
  the length; 1 for `WM_NCCREATE`). Set then get; a buffer of 4 gets 3 characters and a
  terminator; an invalid window.""",
        waits=[WINDOW], header=WH, core=["rt_window_get_text", "rt_window_set_text"]),
    row("window-text-w", G_WIN, ["SetWindowTextW"], ["BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString)"],
        "the record's title, through WM_SETTEXT", WIN_ERR + """\
- `return rt_window_set_text(ctx, hWnd, RT_TEXT_W(lpString));`.
- Tests: as `SetWindowTextA`, with a W window and a procedure that keeps the WCHAR text; read it
  back with `rt_window_get_text(T_CTX(), h, buf, n, true)`.""",
        waits=[WINDOW], header=WH, core=["rt_window_set_text", "rt_window_get_text"]),
    row("is-iconic", G_WIN, ["IsIconic", "IsWindowVisible"],
        ["BOOL IsIconic(HWND hWnd)", "BOOL IsWindowVisible(HWND hWnd)"],
        "no record is minimized; a record is visible once ShowWindow has shown it; the desktop window is visible", WIN_ERR + WIN_TEST + """\
- `return rt_window_is_iconic(hWnd);` and `rt_window_is_visible`.
- Tests: a new record is FALSE in both with last error left alone; after
  `rt_window_show(h, 5)` on a `WS_POPUP` window whose procedure passes everything to
  `rt_window_def_proc`, `IsWindowVisible` is TRUE and `IsIconic` still FALSE, and a `WS_CHILD`
  window of it that was never shown is FALSE; `RT_HWND_DESKTOP` is FALSE and TRUE; an invalid
  window is FALSE with `ERROR_INVALID_WINDOW_HANDLE`.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_is_iconic", "rt_window_is_visible"]),
    row("get-window", G_WIN, ["GetWindow", "GetTopWindow"],
        ["HWND GetWindow(HWND hWnd, UINT uCmd)", "HWND GetTopWindow(HWND hWnd)"],
        "the record's neighbours in the lists the core keeps", WIN_ERR + WIN_TEST + """\
- `return rt_window_get(hWnd, uCmd);` and `return rt_window_top(hWnd);`.
- Tests with two top-level windows A then B and two children C then D of A: `GetTopWindow(NULL)`
  is B and `GW_HWNDNEXT` of B is A (top-level windows are newest first); `GW_CHILD` of A and
  `GetTopWindow(A)` are C and `GW_HWNDNEXT` of C is D (children are in creation order: Wine
  `user32/tests/win.c`, `test_children_zorder`; review D2); `GW_OWNER` of an owned window; no
  such window is NULL with last error left alone; command 6 (`GW_ENABLEDPOPUP`) on A is NULL
  with last error left alone (`test_GW_ENABLEDPOPUP`; review D3); command 77 (the core's code
  for it is from memory); an invalid window.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_get", "rt_window_top"]),
    row("window-thread", G_WIN, ["GetWindowThreadProcessId"],
        ["DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId)"],
        "the creating thread's id and this process's id", WIN_ERR + WIN_TEST + """\
- `return rt_window_thread(hWnd, lpdwProcessId);`.
- Tests: the result equals `CALL(kernel32, GetCurrentThreadId)` and the stored id
  `CALL(kernel32, GetCurrentProcessId)`; a NULL pointer is allowed; an invalid window returns 0
  and leaves a preset DWORD alone.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_thread"]),
    row("find-window", G_WIN, ["FindWindowA"], ["HWND FindWindowA(LPCVOID lpClassName, LPCSTR lpWindowName)"],
        "a top-level window of this process by class and title; NULL for any other program's window",
        WIN_ERR + WIN_TEST + """\
- `return rt_window_find(RT_TEXT_A((const char *)lpClassName), RT_TEXT_A(lpWindowName));` (the
  class may be an atom, hence `LPCVOID`).
- Tests: before any window exists `FindWindowA("Valve001", NULL)` is NULL and last error is
  left alone; then by class, by class in other case, by title, by both, by a title that does not
  match; a title of "" finds a window with no title and not one that has a title (Wine
  `user32/tests/win.c`, `test_FindWindow`); a message-only window is not found (the core's
  rule, from memory: review S17 doubts it).""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_find"]),
    row("enum-windows", G_WIN, ["EnumWindows"], ["BOOL EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam)"],
        "the callback once per top-level window of this process; none exists before the first is created",
        WIN_ERR + WIN_TEST + """\
- `HWND list[256]; unsigned n = rt_window_list_top(list, 256);` more than 256 is
  `RT_UNIMPLEMENTED`. Then for each: `if (!(BOOL)GUEST_CALL(ctx, lpEnumFunc, list[i], lParam))
  return FALSE;` (documented: the enumeration stops and the function returns FALSE; what last
  error is then is the callback's business). After the last: TRUE. A callback of 0 is
  `RT_UNIMPLEMENTED`. Status `partial`.
- Tests: with no window the callback (a `GUEST_FN` that counts and stores `rcx`) is never called
  and the result is TRUE; with two windows it sees both, newest first, and gets `lParam` in rdx;
  a callback returning 0 ends it after the first with FALSE.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_list_top"]),
    row("dlg-item", G_WIN, ["GetDlgItem"], ["HWND GetDlgItem(HWND hDlg, INT nIDDlgItem)"],
        "the child record with that identifier", WIN_ERR + WIN_TEST + """\
- `return rt_window_child_by_id(hDlg, nIDDlgItem);`.
- Tests: a child created with `.style = WS_CHILD, .parent = h, .menu = (HMENU)(uintptr_t)42` is
  found by 42; 43 is NULL with `ERROR_CONTROL_ID_NOT_FOUND` (the core's code; from memory,
  review N10); an invalid parent.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_child_by_id"]),
    row("desktop-window", G_WIN, ["GetDesktopWindow", "GetShellWindow"],
        ["HWND GetDesktopWindow(void)", "HWND GetShellWindow(void)"],
        "the constant RT_HWND_DESKTOP; NULL (no shell runs)", FIXED + """\
- `GetDesktopWindow`: `return RT_HWND_DESKTOP;` (`rt/window.h`), status `full`.
- `GetShellWindow`: `return NULL;`, status `full`: no shell process exists on this machine, and
  NULL is the documented answer when there is no shell window.
- Tests: both values, twice each; last error left alone.""",
        **METRICS),
    row("window-from-point", G_WIN, ["WindowFromPoint"], ["HWND WindowFromPoint(POINT Point)"],
        "the desktop window for a point on the screen, NULL off it (no window is ever visible)", FIXED + """\
- No window is ever visible in this runtime (`rt/window.h`, NO SCREEN), so the only thing under
  a point is the desktop: `const RtDesktop *d = rt_desktop();` a point with `monitor.left <= x <
  monitor.right` and the same for y returns `RT_HWND_DESKTOP`, any other NULL. Status `full`.
- Tests: {0, 0}, {1919, 1079}, {960, 540} give the desktop window; {1920, 0}, {0, 1080},
  {-1, 5} give NULL; last error left alone.""",
        header=WH, core=["rt_desktop"], **METRICS),
    row("active-window", G_WIN, ["GetActiveWindow", "GetFocus"],
        ["HWND GetActiveWindow(void)", "HWND GetFocus(void)"],
        "NULL until a window has been activated, which needs a visible one", """\
- `return rt_window_active();` and `return rt_window_focus();`. Status `full`, `RT-TRAPS: none`.
- Tests: NULL, twice; last error left alone.""",
        header=WH, core=["rt_window_active", "rt_window_focus"], **METRICS),
    row("foreground-window", G_WIN, ["GetForegroundWindow"], ["HWND GetForegroundWindow(void)"],
        "NULL (documented for the moment no window is in the foreground)", """\
- `return rt_window_foreground();`. Status `full`, `RT-TRAPS: none`.
- Tests: NULL, twice; last error left alone.""",
        header=WH, core=["rt_window_foreground"], **METRICS),
    row("allow-foreground", G_WIN, ["AllowSetForegroundWindow"], ["BOOL AllowSetForegroundWindow(DWORD dwProcessId)"],
        "TRUE: there is no other process to give the right to", FIXED + """\
- `return TRUE;` for every argument, status `advisory`, signed off in the contract: the
  function only grants another process a right, and this machine runs no other process. (What
  Windows answers for a process id that does not exist is not settled: review S34.)
  Log once at debug level: `rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG,
  "user32.dll!AllowSetForegroundWindow", "AllowSetForegroundWindow(%u): no other process exists",
  dwProcessId);`.
- Tests: TRUE for 0, 1234 and `(DWORD)-1` (`ASFW_ANY`); last error left alone; the log line.""",
        **METRICS),
    row("set-focus", G_WIN, ["SetFocus", "SetActiveWindow"],
        ["HWND SetFocus(HWND hWnd)", "HWND SetActiveWindow(HWND hWnd)"],
        "NULL handle and invalid handle as documented; a record stops (activation needs a visible window)",
        WIN_ERR + WIN_TEST + """\
- `return rt_window_set_focus(hWnd);` and `rt_window_set_active`.
- Tests: `SetFocus(NULL)` is NULL with last error left alone, and so is `SetActiveWindow(NULL)`
  (Wine `user32/tests/win.c`, `test_SetActiveWindow_0_proc`; review D4); an invalid handle in
  both (`ERROR_INVALID_WINDOW_HANDLE`); the stop for a record in both.""",
        waits=[WINDOW, STATE], header=WH, core=["rt_window_set_focus", "rt_window_set_active"]),
    row("set-foreground", G_WIN, ["SetForegroundWindow"], ["BOOL SetForegroundWindow(HWND hWnd)"],
        "fails for a handle that is no window; a record stops", WIN_ERR + WIN_TEST + """\
- `return rt_window_set_foreground(hWnd);`.
- Tests: NULL and an invalid handle are FALSE with `ERROR_INVALID_WINDOW_HANDLE`; the stop for
  a record.""",
        waits=[WINDOW, STATE], header=WH, core=["rt_window_set_foreground"]),
    row("capture", G_WIN, ["SetCapture", "ReleaseCapture"],
        ["HWND SetCapture(HWND hWnd)", "BOOL ReleaseCapture(void)"],
        "the capture is stored; no mouse message ever arrives", WIN_ERR + WIN_TEST + """\
- `return rt_window_set_capture(hWnd);` and `return rt_window_release_capture();`.
- Tests: the first capture returns NULL; release is TRUE, also when nothing is captured;
  capturing the same window again returns it; an invalid handle; the stop for a second window
  while the first holds the capture.""",
        waits=[WINDOW, STATE], header=WH, core=["rt_window_set_capture", "rt_window_release_capture"]),
    row("show-window", G_WIN, ["ShowWindow", "UpdateWindow"],
        ["BOOL ShowWindow(HWND hWnd, INT nCmdShow)", "BOOL UpdateWindow(HWND hWnd)"],
        "shows a top-level window on its record (commands 1, 5, 10) and hides it (0), with the messages Windows sends; nothing is drawn",
        WIN_ERR + WIN_TEST + """\
- `return rt_window_show(hWnd, nCmdShow);` and `return rt_window_update(hWnd);`.
- Tests, with a `WS_POPUP` window whose procedure records the message numbers and passes
  everything to `rt_window_def_proc(ctx, h, msg, wp, lp, true)` (wrap that call in
  `tf_fp_enter_host();` and `tf_fp_enter_guest(ctx->mxcsr);`, which `rt/shim.h` brings, as
  `runtime/tests/core/win_window.c` does): `SW_HIDE` (0) on the new window is FALSE with last
  error left alone and nothing recorded; command 10 (`SW_SHOWDEFAULT`) is FALSE and records
  exactly 0x18, 0x46, 0x1C, 0x86, 0x06, 0x07, 0x47 (`docs/reviews/win-window-sequences.md` S6),
  and `rt_window_is_visible(h)` is then TRUE and `rt_window_active()` is `h`; command 5 on the
  shown window is nonzero and records nothing (S7); command 0 is then nonzero and records
  0x18, 0x46, 0x47, 0x86, 0x06, 0x1C, 0x08 (S8); an invalid window; `UpdateWindow` is TRUE for
  a record; the stop for command 3 (`T_TRAPS(..., "rt_window_show: not implemented")`).""",
        waits=[WINDOW, STATE], header=WH, core=["rt_window_show", "rt_window_update"]),
    row("invalidate", G_WIN, ["InvalidateRect", "ValidateRect"],
        ["BOOL InvalidateRect(HWND hWnd, LPCRECT lpRect, BOOL bErase)", "BOOL ValidateRect(HWND hWnd, LPCRECT lpRect)"],
        "TRUE and nothing: a window that was never shown has no update region", WIN_ERR + WIN_TEST + """\
- `return rt_window_invalidate(hWnd, lpRect, bErase);` and `return rt_window_validate(hWnd, lpRect);`.
- Tests: a record with a rectangle and with NULL; a NULL window; an invalid window.""",
        waits=[WINDOW, STATE], header=WH, core=["rt_window_invalidate", "rt_window_validate"]),
    row("set-window-pos", G_WIN, ["SetWindowPos"],
        ["BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, INT X, INT Y, INT cx, INT cy, UINT uFlags)"],
        "the record's rectangle and order, with the messages Windows sends; SWP_SHOWWINDOW stops", WIN_ERR + WIN_TEST + """\
- `return rt_window_set_pos(ctx, hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);`.
- Tests (every call passes `SWP_NOACTIVATE` (0x10): without it the hidden window is activated,
  `docs/reviews/win-window-sequences.md` row 3.9, and a procedure that does not pass
  `WM_ACTIVATE` on is left without focus): a move and size is seen by `rt_window_get_rect`;
  `SWP_NOMOVE` keeps the position and `SWP_NOSIZE` the size; a resize with `SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE` of a
  `WS_POPUP` window whose procedure passes everything to `rt_window_def_proc` records
  `WM_WINDOWPOSCHANGING` (0x46), `WM_NCCALCSIZE` (0x83), `WM_WINDOWPOSCHANGED` (0x47), `WM_SIZE`
  (5) and no `WM_MOVE` (Wine `user32/tests/msg.c`, `WmSWP_ResizeSeq`; review D7); an invalid
  window; the stop for `SWP_SHOWWINDOW`.""",
        waits=[WINDOW, QUERY], header=WH, core=["rt_window_set_pos", "rt_window_get_rect"]),
    row("adjust-rect", G_WIN, ["AdjustWindowRectEx"],
        ["BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)"],
        "unchanged for a style with no frame; a style with a frame stops until the piece win-frame", """\
- `RECT in; if (!rt_window_frame(dwStyle, dwExStyle, bMenu, &in)) RT_UNIMPLEMENTED(ctx, "a style
  with a frame: style 0x%x, extended style 0x%x, menu %d", ...);` then grow the rectangle:
  `left -= in.left; top -= in.top; right += in.right; bottom += in.bottom;` and return TRUE.
  A NULL rectangle is `RT_UNIMPLEMENTED` (not documented). Status `partial`. Last error is left
  alone.
- Tests: {100, 100, 740, 580} with `WS_POPUP` (0x80000000) and with `WS_POPUP | WS_CLIPSIBLINGS |
  WS_CLIPCHILDREN | WS_SYSMENU` comes back unchanged and TRUE; the stops for `WS_CAPTION`
  (0x00C00000), for `WS_POPUP | WS_THICKFRAME`, for extended style `WS_EX_CLIENTEDGE`, for a
  menu, for NULL.""",
        header=WH, core=["rt_window_frame"], **METRICS),

    # ---- messages and the queue -------------------------------------------------------------------
    row("peek-message", G_MSG, ["PeekMessageA", "PeekMessageW"],
        ["BOOL PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)",
         "BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)"],
        "FALSE at once on an empty queue, which is every queue nobody posted to", WIN_ERR + """\
- `return rt_msg_peek(ctx, lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, false);` and `true`.
- Tests: on the empty queue FALSE and a MSG preset to 0xA5 bytes untouched, 1,000 times, last
  error left alone; after `rt_msg_post(T_CTX(), NULL, WM_USER + 7, 1, 2)` a peek with
  `PM_NOREMOVE` sees it twice and with `PM_REMOVE` once (hwnd NULL, the three values); a filter
  range that excludes it; two messages come out in the order posted; a peek with `(HWND)-1`
  takes a thread message, and a peek with `(HWND)0x40000FF0` is FALSE with
  `ERROR_INVALID_WINDOW_HANDLE`, the MSG untouched (Wine `user32/tests/msg.c`,
  `test_PostMessage` and `test_invalid_window`; review D5); the stops for a NULL MSG and for
  a flag above `PM_NOYIELD`. The window filter is proved in `tests/core/win_queue.c`: a shim
  test cannot make a window before the piece `win-window` is built.""",
        waits=[QUEUE], header=WH, core=["rt_msg_peek", "rt_msg_post"]),
    row("get-message", G_MSG, ["GetMessageA"],
        ["BOOL GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)"],
        "waits for a posted message; on a queue nobody posts to it waits for ever, as on Windows", WIN_ERR + """\
- `return rt_msg_get(ctx, lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, false);`.
- Tests post first, so that nothing waits: a posted `WM_USER` message returns 1 with its values;
  a posted `WM_QUIT` (0x12) returns 0; a window handle that is no window returns -1. Do not
  call it on an empty queue: the test would hang.""",
        waits=[QUEUE], header=WH, core=["rt_msg_get", "rt_msg_post"]),
    row("dispatch-message", G_MSG, ["DispatchMessageA", "DispatchMessageW"],
        ["LRESULT DispatchMessageA(const MSG *lpMsg)", "LRESULT DispatchMessageW(const MSG *lpMsg)"],
        "calls the window's procedure; 0 for a message with no window", WIN_ERR + WIN_TEST + """\
- `return rt_window_dispatch(ctx, lpMsg, false);` and `true`.
- Tests: a MSG with `hwnd` NULL returns 0; a MSG for a window whose procedure returns
  `wParam + lParam` for message `WM_USER` returns the sum; an invalid window.""",
        waits=[WINDOW], header=WH, core=["rt_window_dispatch"]),
    row("translate-message", G_MSG, ["TranslateMessage"], ["BOOL TranslateMessage(const MSG *lpMsg)"],
        "FALSE for every message that is not a key message; a key message stops (no keyboard before Gate 4)",
        FIXED + """\
- Documented: the return value is zero when the message is not translated, and only the four
  key messages are. So: `lpMsg` NULL is `RT_UNIMPLEMENTED`; `message` one of `WM_KEYDOWN`
  (0x100), `WM_KEYUP` (0x101), `WM_SYSKEYDOWN` (0x104), `WM_SYSKEYUP` (0x105) is
  `RT_UNIMPLEMENTED(ctx, "a key message 0x%x: character translation needs the keyboard tables
  of Gate 4", ...)`; anything else `return FALSE;`. The MSG is never written. Status `partial`.
- Tests: FALSE for `WM_NULL`, `WM_USER`, `WM_CHAR` (0x102), 0xC001; the MSG unchanged; last error
  left alone; the stops for the four key messages and for NULL.""",
        **METRICS),
    row("translate-accel", G_MSG, ["TranslateAcceleratorA"],
        ["INT TranslateAcceleratorA(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg)"],
        "0: no accelerator table can exist (nothing that makes one is imported)", FIXED + """\
- No boot-path DLL imports a function that creates or loads an accelerator table, so every
  `HACCEL` is NULL or invalid: `return FAIL(ctx, ERROR_INVALID_ACCEL_HANDLE, 0);` for every
  argument (the code is Wine's, from memory). Status `absent`; log once at debug level
  (`rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "user32.dll!TranslateAcceleratorA",
  "TranslateAcceleratorA: no accelerator table exists")`). Nothing is read or written.
- Tests: 0 and the code for a NULL table and for `(HACCEL)0x1234`, with a NULL and a non-NULL
  MSG (preset bytes unchanged); the log line.""",
        **METRICS),
    row("post-message", G_MSG, ["PostMessageA"],
        ["BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)"],
        "the message goes into the owner thread's queue", WIN_ERR + """\
- `return rt_msg_post(ctx, hWnd, Msg, wParam, lParam);`.
- Tests: post with a NULL window, then `rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_REMOVE, false)`
  gives the same four values; an invalid window is FALSE with `ERROR_INVALID_WINDOW_HANDLE`
  and nothing is queued; the stop for `HWND_BROADCAST` (0xFFFF).""",
        waits=[QUEUE], header=WH, core=["rt_msg_post", "rt_msg_peek"]),
    row("send-message", G_MSG, ["SendMessageA"],
        ["LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)"],
        "calls the procedure of a window of the calling thread and returns its result", WIN_ERR + WIN_TEST + """\
- `return rt_window_send(ctx, hWnd, Msg, wParam, lParam, false);`.
- Tests: a procedure that returns `wParam * 2` for `WM_USER`; an invalid window returns 0 with
  `ERROR_INVALID_WINDOW_HANDLE`; the stop for `HWND_BROADCAST`.""",
        waits=[WINDOW], header=WH, core=["rt_window_send"]),
    row("register-message", G_MSG, ["RegisterWindowMessageA"], ["UINT RegisterWindowMessageA(LPCSTR lpString)"],
        "a message number from 0xC000 up, the same for the same string", WIN_ERR + """\
- `return rt_wndmsg_register(RT_TEXT_A(lpString));`.
- Tests: a value in 0xC000 to 0xFFFF and the same value for the same string
  (`RegisterWindowMessageA`'s page; review Q4); the same string in other case gives it too (the
  core's rule, from memory: S22), another string another; last error left alone; the stops for
  NULL and "".""",
        header=WH, core=["rt_wndmsg_register"]),
    row("msg-wait", G_MSG, ["MsgWaitForMultipleObjects"],
        ["DWORD MsgWaitForMultipleObjects(DWORD nCount, const HANDLE *pHandles, BOOL fWaitAll, "
         "DWORD dwMilliseconds, DWORD dwWakeMask)"],
        "WaitForMultipleObjects with the same time limit, woken also by a posted message", WIN_ERR + """\
- `return rt_msg_wait(ctx, nCount, pHandles, fWaitAll, dwMilliseconds, dwWakeMask);`.
- Tests (`QS_ALLINPUT` is 0x04FF): no handle and 20 ms gives `WAIT_TIMEOUT` after at least 20 ms
  on `rt_time_mono_100ns` (`rt/time.h`); a set event (`rt_event_create(T_CTX(), TRUE, TRUE, NULL)`,
  `rt/sync.h`) gives `WAIT_OBJECT_0`; after `rt_msg_post(T_CTX(), NULL, WM_USER, 0, 0)`, one
  handle that is not set gives `WAIT_OBJECT_0 + 1`; then call `rt_msg_peek` with `PM_NOREMOVE`
  (it must come before the next wait: a second wait with no look between is a stop of the
  core) and the same wait with 20 ms times out (the message is no longer new); a handle that
  is not open (0x7654) gives `WAIT_FAILED` with `ERROR_INVALID_HANDLE`; the stop for a wake
  mask of 0.""",
        waits=[QUEUE], header=WH, core=["rt_msg_wait", "rt_msg_post", "rt_msg_peek"]),
    row("message-time", G_MSG, ["GetMessageTime"], ["LONG GetMessageTime(void)"],
        "the time of the last message the thread took; 0 before the first", WIN_ERR + """\
- `return rt_msg_time();`.
- Tests: 0 at first; after a post and a removing peek it equals the `time` member of that MSG.""",
        waits=[QUEUE], header=WH, core=["rt_msg_time", "rt_msg_post", "rt_msg_peek"]),

    # ---- metrics and monitors ---------------------------------------------------------------------
    row("system-metrics", G_DESK, ["GetSystemMetrics"], ["INT GetSystemMetrics(INT nIndex)"],
        "the screen of the traced run (1920x1080, one monitor); a metric outside the table stops", """\
- `INT v; if (!rt_desktop_metric(nIndex, &v)) RT_UNIMPLEMENTED(ctx, "a metric the core does not
  answer: %d", nIndex); return v;` Status `partial`, `RT-TRAPS: a metric the core does not answer`
  (an `RT-TRAPS` entry must be a piece of the message's text). Last error is never set.
- Tests: every index of the core's table with its value; last error left alone; the stops for
  the indexes 4, 19 and 10000, written as numbers (`rt/win/user.h` names only the metrics that
  are answered; 4 is the caption height, 19 "a mouse is present").""",
        header=WH, core=["rt_desktop_metric"]),
    row("system-params", G_DESK, ["SystemParametersInfoA"],
        ["BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, LPVOID pvParam, UINT fWinIni)"],
        "SPI_GETWORKAREA is the monitor's rectangle (no taskbar); any other action stops", FIXED + """\
- `uiAction == SPI_GETWORKAREA` (0x30) with `pvParam` not NULL: copy `rt_desktop()->work` to the
  RECT at `pvParam` and return TRUE; last error left alone. Every other action, and a NULL
  `pvParam`, is `RT_UNIMPLEMENTED(ctx, "action 0x%x", uiAction)`: a system parameter is never
  guessed. Status `partial`.
- Tests: the RECT is {0, 0, 1920, 1080}, the 8 bytes after it stay 0xA5; the stops for actions
  0x68 (`SPI_GETWHEELSCROLLLINES`) and 0x30 with NULL.""",
        header=WH, core=["rt_desktop"], **METRICS),
    row("enum-monitors", G_DESK, ["EnumDisplayMonitors"],
        ["BOOL EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData)"],
        "the callback once, for the one monitor", FIXED + """\
- `hdc` not NULL, or `lpfnEnum` 0: `RT_UNIMPLEMENTED`. With `lprcClip` NULL, or a clip rectangle
  that intersects the monitor (`rt_desktop()->monitor`), call the callback once:
  `RECT r = d->monitor; BOOL go = (BOOL)GUEST_CALL(ctx, lpfnEnum, RT_HMONITOR_PRIMARY, NULL, &r,
  dwData);` and return TRUE if it returned nonzero, FALSE if it returned 0 (documented: returning
  FALSE stops the enumeration and makes the function return FALSE). A clip rectangle that
  misses the monitor: no call, TRUE (from memory: review S33 of `contracts-check-2.md`, not
  settled). The RECT handed to the callback is a copy on the shim's
  stack, which is guest-readable. Status `partial`. Last error is left alone.
- Tests with a `GUEST_FN` that stores rcx, rdx, the four LONGs at r8 and r9, counts, and returns
  what a static says: one call with the handle 0x10001, an HDC of NULL, {0, 0, 1920, 1080} and the
  data; a clip of {100, 100, 200, 200} still calls; a clip of {5000, 0, 6000, 10} does not; a
  callback that returns 0 gives FALSE; the two stops.""",
        header=WH, core=["rt_desktop"], **METRICS),
    row("display-settings", G_DESK, ["ChangeDisplaySettingsA"],
        ["LONG ChangeDisplaySettingsA(LPCVOID lpDevMode, DWORD dwFlags)"],
        "restoring the default mode succeeds (nothing was changed); a mode change stops", FIXED + """\
- `lpDevMode` is `LPCVOID` here (the documentation's `DEVMODEA *` is not defined in this
  runtime). `lpDevMode == NULL && dwFlags == 0` is the documented way to return to the default
  mode: `return DISP_CHANGE_SUCCESSFUL;` (0), nothing else happens. Anything else is
  `RT_UNIMPLEMENTED(ctx, "a display mode change (flags 0x%x)", dwFlags)`. Status `partial`.
- Tests: 0 for (NULL, 0), twice, last error left alone; the stops for a non-NULL mode and for
  (NULL, 4).""",
        **METRICS),
    row("window-station", G_DESK, ["GetProcessWindowStation", "GetUserObjectInformationW"],
        ["HWINSTA GetProcessWindowStation(void)",
         "BOOL GetUserObjectInformationW(HANDLE hObj, INT nIndex, LPVOID pvInfo, DWORD nLength, LPDWORD lpnLengthNeeded)"],
        "the interactive window station, as a constant, and its flags (visible)", FIXED + """\
- `GetProcessWindowStation`: `return RT_HWINSTA;`, status `full`.
- `GetUserObjectInformationW`: only `hObj == RT_HWINSTA` with `nIndex == UOI_FLAGS` (1) is
  answered, the question the C runtime asks before a message box. `pvInfo` NULL or `nLength`
  below 12: store 12 in `*lpnLengthNeeded` if it is not NULL and `return FAIL(ctx,
  ERROR_INSUFFICIENT_BUFFER, FALSE);`. Otherwise write a `USEROBJECTFLAGS` (`rt/win/user.h`):
  `fInherit = FALSE, fReserved = FALSE, dwFlags = WSF_VISIBLE`, store 12 in `*lpnLengthNeeded` if
  not NULL, return TRUE. Any other handle or index: `RT_UNIMPLEMENTED` with both. Status `partial`.
- Tests: the constant; the three members and the stored length; the 4 bytes after the 12 stay
  0xA5; a length of 8 fails with the code, stores 12 and writes nothing; a NULL length pointer is
  allowed; the stops for index 2 and for another handle.""",
        **METRICS),

    # ---- device contexts, fonts and bitmaps -------------------------------------------------------
    row("get-dc", G_GDI, ["GetDC", "ReleaseDC"], ["HDC GetDC(HWND hWnd)", "INT ReleaseDC(HWND hWnd, HDC hDC)"],
        "a display-context record for the screen or a window; released again", WIN_ERR + """\
- `return rt_dc_get(hWnd);` and `return rt_dc_release(hWnd, hDC);`.
- Tests: `GetDC(NULL)` gives a handle of 0x40000000 or above; `rt_dc_device_cap(dc, HORZRES)` is
  1920; release is 1 and a second release 0; a context of `GetDC(NULL)` released with ANOTHER
  window value, `ReleaseDC(RT_HWND_DESKTOP, dc)`, is 1 as well, the window is not compared
  (Wine `user32/tests/dce.c`, `test_parameters`; review D12); `ReleaseDC(NULL, NULL)` is 0;
  `GetDC` of a handle that is no window is NULL with `ERROR_INVALID_WINDOW_HANDLE`.""",
        waits=[GDIDC], header=GH, core=["rt_dc_get", "rt_dc_release", "rt_dc_device_cap"]),
    row("compatible-dc", G_GDI, ["CreateCompatibleDC", "DeleteDC"],
        ["HDC CreateCompatibleDC(HDC hdc)", "BOOL DeleteDC(HDC hdc)"],
        "a memory-context record; deleted again", WIN_ERR + """\
- `return rt_dc_create_compatible(hdc);` and `return rt_dc_delete(hdc);`.
- Tests: from NULL and from a display context (`rt_dc_get(NULL)`); from a handle that is no
  context NULL; delete is TRUE once and FALSE the second time; the stop for deleting a context
  of `rt_dc_get`.""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_create_compatible", "rt_dc_delete", "rt_dc_get"]),
    row("create-dc", G_GDI, ["CreateDCW"],
        ["HDC CreateDCW(LPCWSTR pwszDriver, LPCWSTR pwszDevice, LPCWSTR pszPort, LPCVOID pdm)"],
        "a display-context record for the display or its adapter; NULL for any other device", WIN_ERR + """\
- `pszPort` and `pdm` not NULL are `RT_UNIMPLEMENTED` (the documentation says the port is
  ignored and must be NULL; an initialisation mode is a mode change). Else
  `return rt_dc_create_display(pwszDriver, pwszDevice);`. Status `partial`.
- Tests: (L"DISPLAY", NULL), (L"display", NULL), (L"DISPLAY", L"xyz") and (NULL, L"\\\\\\\\.\\\\DISPLAY1")
  give a context whose `rt_dc_device_cap(dc, BITSPIXEL)` is 32; (NULL, NULL), (NULL, L"display")
  and (L"WINSPOOL", L"Printer") give NULL (Wine `gdi32/tests/dc.c`, `test_CreateDC`; review D13);
  delete each with `rt_dc_delete`; the two stops.""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_create_display", "rt_dc_device_cap", "rt_dc_delete"]),
    row("device-caps", G_GDI, ["GetDeviceCaps"], ["INT GetDeviceCaps(HDC hdc, INT index)"],
        "the display of the traced run: 1920x1080, 32 bits, 96 dpi, 60 Hz; another index stops", WIN_ERR + """\
- `return rt_dc_device_cap(hdc, index);`.
- Tests on `rt_dc_get(NULL)` and on a memory context: `HORZRES` 1920, `VERTRES` 1080, `BITSPIXEL`
  32, `PLANES` 1, `LOGPIXELSX` and `LOGPIXELSY` 96, `VREFRESH` 60; a handle that is no context
  (`(HDC)0xdeadbeef` after `SetLastError(0xdeadbeef)`) gives 0 with last error 6,
  `ERROR_INVALID_HANDLE` (Wine `gdi32/tests/dc.c`, `test_dc_values`); the stop for index 38
  (`RASTERCAPS`).""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_device_cap", "rt_dc_get", "rt_dc_create_compatible"]),
    row("select-object", G_GDI, ["SelectObject"], ["HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h)"],
        "the context remembers the object and returns the one before", WIN_ERR + """\
- `return rt_dc_select(hdc, h);`.
- Tests on a memory context: a brush returns NULL the first time and the first brush when a
  second is selected; a pen and a font have slots of their own; a bitmap into a display context
  is NULL; a handle that is no object is NULL; `SelectObject((HDC)0x40000FF0, brush)` is NULL
  with last error 6, `ERROR_INVALID_HANDLE` (Wine `gdi32/tests/gdiobj.c`, `test_gdi_objects`;
  review D14); the stop for a region.""",
        lib="gdi32", waits=[GDIDC, GDIOBJ], header=GH,
        core=["rt_dc_select", "rt_dc_create_compatible", "rt_gdi_create_brush", "rt_gdi_create_pen"]),
    row("bk-color", G_GDI, ["SetBkColor", "GetBkColor"],
        ["COLORREF SetBkColor(HDC hdc, COLORREF color)", "COLORREF GetBkColor(HDC hdc)"],
        "the context's background colour, stored; white at first", WIN_ERR + """\
- Set: `ULONG v = color, old; return rt_dc_attr(hdc, RT_DC_BK_COLOR, &v, &old) ? old : CLR_INVALID;`
  Get: `ULONG old; return rt_dc_attr(hdc, RT_DC_BK_COLOR, NULL, &old) ? old : CLR_INVALID;`
- Tests on a memory context: the first get is 0x00FFFFFF; a set returns it and a get returns
  the new colour; a handle that is no context gives `CLR_INVALID` (0xFFFFFFFF) in both, with
  last error `ERROR_INVALID_HANDLE` as the core's comment says (the return is what Wine's
  `gdi32/tests/dc.c` asserts for `GetBkColor`; the code is from memory for the colours).""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_attr", "rt_dc_create_compatible"]),
    row("text-color", G_GDI, ["SetTextColor", "GetTextColor"],
        ["COLORREF SetTextColor(HDC hdc, COLORREF color)", "COLORREF GetTextColor(HDC hdc)"],
        "the context's text colour, stored; black at first", WIN_ERR + """\
- As `SetBkColor` and `GetBkColor`, with `RT_DC_TEXT_COLOR`.
- Tests: as there; the first get is 0.""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_attr", "rt_dc_create_compatible"]),
    row("bk-mode", G_GDI, ["SetBkMode", "SetTextAlign"],
        ["INT SetBkMode(HDC hdc, INT mode)", "UINT SetTextAlign(HDC hdc, UINT align)"],
        "the context's background mode and text alignment, stored", WIN_ERR + """\
- `SetBkMode`: `ULONG v = (ULONG)mode, old; return rt_dc_attr(hdc, RT_DC_BK_MODE, &v, &old) ?
  (INT)old : 0;`. `SetTextAlign`: the same with `RT_DC_TEXT_ALIGN`, and 0xFFFFFFFF (`GDI_ERROR`)
  on failure.
- Tests on a memory context: `SetBkMode(TRANSPARENT)` returns `OPAQUE` (2), then `TRANSPARENT`
  (1) is returned by the next set; mode 7 returns 0 and changes nothing; `SetTextAlign(6)`
  returns 0 and the next returns 6; a handle that is no context gives 0 and 0xFFFFFFFF, each
  with last error 6 after `SetLastError(0xdeadbeef)` (`ERROR_INVALID_HANDLE`: Wine
  `gdi32/tests/dc.c`, `test_dc_values`, asserts it for `SetBkMode((HDC)0xdeadbeef, OPAQUE)`;
  for `SetTextAlign` it is the core's rule, from memory).""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_attr", "rt_dc_create_compatible"]),
    row("stretch-mode", G_GDI, ["SetStretchBltMode"], ["INT SetStretchBltMode(HDC hdc, INT mode)"],
        "the context's stretch mode, stored", WIN_ERR + """\
- `ULONG v = (ULONG)mode, old; return rt_dc_attr(hdc, RT_DC_STRETCH_MODE, &v, &old) ? (INT)old : 0;`
- Tests: `COLORONCOLOR` (3) returns `BLACKONWHITE` (1), the next set returns 3; mode 9 returns 0
  with `ERROR_INVALID_PARAMETER`; a handle that is no context returns 0 with
  `ERROR_INVALID_HANDLE` (the core's rule; from memory for this function).""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_attr", "rt_dc_create_compatible"]),
    row("dc-points", G_GDI, ["SetViewportOrgEx", "MoveToEx"],
        ["BOOL SetViewportOrgEx(HDC hdc, INT x, INT y, LPPOINT lppt)", "BOOL MoveToEx(HDC hdc, INT x, INT y, LPPOINT lppt)"],
        "the context's viewport origin and current position, stored", WIN_ERR + """\
- `return rt_dc_set_point(hdc, RT_DC_VIEWPORT_ORG, x, y, lppt);` and `RT_DC_POSITION`.
- Tests on a memory context: the first call stores {0, 0} in the POINT, the second the first
  call's values; a NULL POINT is allowed; the two points are independent; a handle that is no
  context is FALSE with `ERROR_INVALID_HANDLE` (the core's rule; from memory for these two)
  and leaves the POINT alone.""",
        lib="gdi32", waits=[GDIDC], header=GH, core=["rt_dc_set_point", "rt_dc_create_compatible"]),
    row("pen-brush", G_GDI, ["CreatePen", "CreateSolidBrush"],
        ["HPEN CreatePen(INT iStyle, INT cWidth, COLORREF color)", "HBRUSH CreateSolidBrush(COLORREF color)"],
        "an object record that remembers how it was made", WIN_ERR + """\
- `return rt_gdi_create_pen(iStyle, cWidth, color);` and `return rt_gdi_create_brush(color);`.
- Tests: a handle of 0x40000000 or above each; `rt_gdi_get_object(h, 16, &lp, false)` gives the
  style, {width, 0} and colour of the pen, and `BS_SOLID`, the colour and 0 of the brush;
  `CreatePen(0, -3, 0x123456)` reads back width 3 and `CreatePen(5, 7, 0x123456)` (`PS_NULL`)
  reads back {5, {1, 0}, 0} (Wine `gdi32/tests/pen.c`, `test_logpen`; review D15);
  `rt_gdi_delete` each; the stop for pen style 9.""",
        lib="gdi32", waits=[GDIOBJ], header=GH,
        core=["rt_gdi_create_pen", "rt_gdi_create_brush", "rt_gdi_get_object", "rt_gdi_delete"]),
    row("create-font", G_GDI, ["CreateFontA"],
        ["HFONT CreateFontA(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, "
         "DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, "
         "DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)"],
        "a font record that is its LOGFONT; no font is matched or loaded", WIN_ERR + """\
- Fill a zeroed `LOGFONTA` (`rt/win/gdi.h`): the five INTs into the five LONG members, the low
  byte of each of the eight DWORDs into the eight BYTE members in order, and the face name
  copied up to 31 bytes (NULL: left empty). `return rt_gdi_create_font_a(&lf);`.
- Tests: create with distinct values in all 14 arguments, read back with
  `rt_gdi_get_object(h, 60, &lf, false)` and compare every member; a NULL face name; a face name
  of 40 bytes arrives as its first 31; `rt_gdi_delete`.""",
        lib="gdi32", waits=[GDIOBJ], header=GH, core=["rt_gdi_create_font_a", "rt_gdi_get_object", "rt_gdi_delete"]),
    row("create-bitmap", G_GDI, ["CreateBitmap", "CreateCompatibleBitmap"],
        ["HBITMAP CreateBitmap(INT nWidth, INT nHeight, UINT nPlanes, UINT nBitCount, LPCVOID lpBits)",
         "HBITMAP CreateCompatibleBitmap(HDC hdc, INT cx, INT cy)"],
        "a bitmap record with its size and format and no pixels", WIN_ERR + """\
- `return rt_gdi_create_bitmap(nWidth, nHeight, nPlanes, nBitCount, lpBits);` and
  `return rt_gdi_create_compatible_bitmap(hdc, cx, cy);`.
- Tests, reading each back with `rt_gdi_get_object(h, 32, &bm, false)`: 64 x 32 at 32 bits has
  `bmWidthBytes` 256 and `bmBits` NULL; a compatible bitmap of a display context
  (`rt_dc_get(NULL)`) is 32 bits, of a fresh memory context 1 bit; a size of 0 x 0 gives 1 x 1 at
  1 bit; a handle that is no context gives NULL; the stop for initial bits.""",
        lib="gdi32", waits=[GDIOBJ, GDIDC], header=GH,
        core=["rt_gdi_create_bitmap", "rt_gdi_create_compatible_bitmap", "rt_gdi_get_object", "rt_dc_get"]),
    row("dib-section", G_GDI, ["CreateDIBSection"],
        ["HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, LPVOID *ppvBits, HANDLE hSection, "
         "DWORD offset)"],
        "a bitmap record whose pixels are zero-filled guest memory the caller draws into itself", WIN_ERR + """\
- `return rt_gdi_create_dib_section(hdc, pbmi, usage, ppvBits, hSection, offset);`.
- Tests: 100 x 50 at 32 bits, `biHeight` -50 (top-down): a handle, a pointer, 20,000 bytes of it
  readable, zero and writable; `rt_gdi_get_object(h, 104, &ds, false)` returns 104 with
  `dsBm.bmBits` equal to the pointer, `dsBm.bmWidthBytes` 400 and `dsBmih.biHeight` and
  `dsBm.bmHeight` 50 (the absolute value: review Z3 of `win-behaviour-check-5.md`, D16 of
  `contracts-check-2.md`); 4 x 4 at 16 bits `BI_RGB` reports `dsBmih.biCompression` 3
  (`BI_BITFIELDS`) and `dsBitfields` {0x7C00, 0x3E0, 0x1F} (Wine `gdi32/tests/dib.c`,
  `test_simple_graphics`; D16); 3 x 1 at 24 bits has 12 bytes a row; a NULL `pbmi`, a width of
  0 and a `biSize` of 39 give NULL with `ERROR_INVALID_PARAMETER` and a NULL pointer;
  `rt_gdi_delete`; the stops for a section handle and for a `biSize` of 12 (a
  `BITMAPCOREHEADER`, which Windows accepts: `gdi32/tests/bitmap.c`,
  `test_bitmapinfoheadersize`; D18).""",
        lib="gdi32", waits=[GDIOBJ], header=GH, core=["rt_gdi_create_dib_section", "rt_gdi_get_object", "rt_gdi_delete"]),
    row("rect-rgn", G_GDI, ["CreateRectRgn", "SetRectRgn"],
        ["HRGN CreateRectRgn(INT x1, INT y1, INT x2, INT y2)",
         "BOOL SetRectRgn(HRGN hrgn, INT left, INT top, INT right, INT bottom)"],
        "a region record that is one rectangle", WIN_ERR + """\
- `return rt_gdi_create_rect_rgn(x1, y1, x2, y2);` and `return rt_gdi_set_rect_rgn(hrgn, left, top,
  right, bottom);`.
- Tests: a handle; set on it is TRUE; set on a brush (`rt_gdi_create_brush`) and on NULL is
  FALSE; `rt_gdi_delete`.""",
        lib="gdi32", waits=[GDIOBJ], header=GH,
        core=["rt_gdi_create_rect_rgn", "rt_gdi_set_rect_rgn", "rt_gdi_create_brush", "rt_gdi_delete"]),
    row("delete-object", G_GDI, ["DeleteObject"], ["BOOL DeleteObject(HGDIOBJ ho)"],
        "the record is gone", WIN_ERR + """\
- `return rt_gdi_delete(ho);`.
- Tests: a brush is TRUE once and FALSE the second time; NULL and a window-table handle of
  another kind are FALSE; the stop for a brush that is selected into a memory context.""",
        lib="gdi32", waits=[GDIOBJ, GDIDC], header=GH,
        core=["rt_gdi_delete", "rt_gdi_create_brush", "rt_dc_create_compatible", "rt_dc_select"]),
    row("get-object", G_GDI, ["GetObjectA", "GetObjectW"],
        ["INT GetObjectA(HANDLE h, INT c, LPVOID pv)", "INT GetObjectW(HANDLE h, INT c, LPVOID pv)"],
        "what the record was made from: LOGPEN, LOGBRUSH, LOGFONT, BITMAP or DIBSECTION", WIN_ERR + """\
- `return rt_gdi_get_object(h, c, pv, false);` and `true`.
- Tests: a pen (16 bytes), a brush (16), a bitmap (32), a font (60 through A, 92 through W, the
  face name as WCHARs); a NULL buffer returns the size; a buffer one byte short returns 0 and
  is not written; a handle that is no object returns 0. A DIB section
  (`rt_gdi_create_dib_section`, width 16, height -16, 32 bits): `GetObjectW(h, 104, &ds)` returns
  104 with `ds.dsBmih.biHeight` 16, positive although the section is top-down, and
  `GetObjectW(h, 0, NULL)` returns 32, not 104 (Wine `gdi32/tests/bitmap.c`, `test_dib_info`;
  `docs/reviews/win-behaviour-check-5.md`, Z3).""",
        lib="gdi32", waits=[GDIOBJ], header=GH,
        core=["rt_gdi_get_object", "rt_gdi_create_pen", "rt_gdi_create_brush", "rt_gdi_create_bitmap",
              "rt_gdi_create_font_a"]),
    row("gdi-absent", G_GDI, ["ExtEscape", "GetDeviceGammaRamp"],
        ["INT ExtEscape(HDC hdc, INT iEscape, INT cjInput, LPCVOID lpInData, INT cjOutput, LPVOID lpOutData)",
         "BOOL GetDeviceGammaRamp(HDC hdc, LPVOID lpRamp)"],
        "0: the display driver implements no escape; FALSE: the display has no loadable gamma ramp", FIXED + """\
- Status `absent` for both, signed off in the contract. `ExtEscape`: `return 0;` for every
  argument: the documented return value for an escape that is not implemented, and this
  machine's display driver implements none (the escapes the game's AMD library sends are for
  an AMD driver). `GetDeviceGammaRamp`: `return FALSE;`: the documentation says that not every
  display supports a loadable gamma ramp, and this one does not. Neither reads or writes a
  buffer; neither sets last error. Each logs once at debug level:
  `rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "gdi32.dll!<Name>", "<Name>: the display has no such
  feature");` (`ExtEscape` adds the escape number: `"ExtEscape(%d): ..."`).
- The data pointers are `LPCVOID` and `LPVOID` in the `SHIM` lines: they are binary, and the
  call trace must not print them as text.
- Tests: 0 for escapes 8 (`QUERYESCSUPPORT`) and 0x110000 with buffers preset to 0xA5 that stay
  so; FALSE with a 1,536-byte ramp buffer untouched; with a NULL and a non-NULL context; last
  error left alone; both log lines.""",
        lib="gdi32", **METRICS),

    # ---- cursors and icons ------------------------------------------------------------------------
    row("load-cursor", G_CUR, ["LoadCursorA", "LoadIconA"],
        ["HCURSOR LoadCursorA(HINSTANCE hInstance, LPCVOID lpCursorName)", "HICON LoadIconA(HINSTANCE hInstance, LPCVOID lpIconName)"],
        "the system cursors and icons as constants; an icon of the executable is not there (NULL); "
        "one from a DLL's resources stops", FIXED + """\
- The name is `LPCVOID`: for a system cursor it is a number below 0x10000 (`MAKEINTRESOURCE`),
  not text. `hInstance == NULL` and the name's value one of the `IDC_` values of
  `rt/win/user.h` (32512 to 32516, 32642 to 32646, 32648 to 32651): `return
  RT_HCURSOR_SYSTEM(id);` (`rt/window.h`). `LoadIconA` likewise with the `IDI_` values (32512 to
  32518) and `RT_HICON_SYSTEM(id)`. `hInstance == NULL` with any other name: `return FAIL(ctx,
  ERROR_RESOURCE_NAME_NOT_FOUND, NULL);` (1814, `rt/win/user.h`; from memory for this call).
- `LoadCursorA` with `hInstance` not NULL: `RT_UNIMPLEMENTED(ctx, "a cursor from the resources of
  module %p (name %p): resource cursors arrive at Gate 4", ...)`.
- `LoadIconA` with `hInstance` not NULL (the orchestrator's decision of 10 October, after the load
  of `vgui2` asked `LoadIconA(GetModuleHandleA(NULL), 101)` and stored the answer unchecked in a
  window class, where a NULL icon is legal):
  - `hInstance` is the executable, `(HINSTANCE)(uintptr_t)rt_module_exe()->base` (`rt/module.h`):
    the main module is our own program, which has no resources, and the original's executable is
    never read. The answer is the one for a resource that is not there: one line
    `rt_log(RT_CH_WINDOW, RT_LL_WARN, "LoadIconA: icon %u of the executable (module %p) is not
    there: this program has no resources and the original's are never read; NULL, last error
    1814", id, hInstance)` (for a name that is text, a pointer of 0x10000 or more: `icon \"%.64s\"`),
    then `return FAIL(ctx, ERROR_RESOURCE_NAME_NOT_FOUND, NULL);`. Where the code comes from:
    Wine's `kernel32/tests/resource.c` (`test_find_resource`) asserts 1814 from `FindResourceW`
    for a name its module does not have under a type it has, and 1813
    (`ERROR_RESOURCE_TYPE_NOT_FOUND`) for a type it does not have; `user32/tests/resource.c`
    asserts 1814 for a missing string (`docs/reviews/contracts-check-2.md` Q1 confirms the
    `FindResource` codes). That `LoadIcon` reports what the resource lookup set is from memory
    (its page names no code and no Wine test loads a missing icon: Q2 of that review, not
    settled), and 1814 rather than 1813 or 1812 is the contract's choice: nothing reads the
    code on the path seen.
  - any other value (a game DLL's handle: a lifted module with a resource section; a system
    library; no module at all): `RT_UNIMPLEMENTED(ctx, "an icon from the resources of module %p
    (name %p): only the executable's are answered", ...)`, until a caller appears.
  Status `partial`.
- Tests: `IDC_ARROW` gives `(HCURSOR)0x17F00` and `IDI_APPLICATION` `(HICON)0x27F00`; the same
  handle on a second call; every id of the two lists; id 100 fails with the code; the stop for
  a module handle (`LoadCursorA`: the executable's; `LoadIconA`: `rt_module_exe()->base + 0x1000`
  and 0x400000); `LoadIconA` with the executable's handle and id 101: NULL, last error 1814, and
  `T_LOGGED(RT_CH_WINDOW, "LoadIconA: icon 101 of the executable")`; the same with a text name.""",
        **METRICS),
    row("set-cursor", G_CUR, ["SetCursor", "ShowCursor"],
        ["HCURSOR SetCursor(HCURSOR hCursor)", "INT ShowCursor(BOOL bShow)"],
        "the current cursor and the display counter, stored", WIN_ERR + """\
- `return rt_cursor_set(hCursor);` and `return rt_cursor_show(bShow);`.
- Tests: the first `SetCursor(RT_HCURSOR_SYSTEM(IDC_ARROW))` returns NULL, `SetCursor(NULL)` then
  returns the arrow; `ShowCursor(FALSE)` gives -1, `TRUE` 0, `TRUE` 1; the stop for a cursor
  handle of 0x1234.""",
        waits=[INPUT], header=WH, core=["rt_cursor_set", "rt_cursor_show"]),
    row("destroy-cursor", G_CUR, ["DestroyCursor"], ["BOOL DestroyCursor(HCURSOR hCursor)"],
        "TRUE for a system cursor, which stays what it was; any other value stops", FIXED + """\
- Review census-rows H3 (this function was a stop with the reason "no cursor exists that it
  could destroy", which is wrong: `LoadCursorA` gives out the system cursors, the input system
  loads 13 of them and destroys 13 when the game shuts down, 267.793 s of the recording).
  Windows: `DestroyCursor` on the cursor of `LoadCursor(NULL, IDC_ARROW)` returns TRUE, leaves
  last error unchanged, and the cursor stays usable: `LoadCursor(NULL, IDC_ARROW)` gives the
  same handle again (Wine `user32/tests/cursoricon.c`, `test_DestroyCursor`, its last block;
  Microsoft's page says not to destroy a shared cursor and names no failure).
- `hCursor` is `RT_HCURSOR_SYSTEM(id)` (`rt/window.h`) for one of the `IDC_` values of
  `rt/win/user.h` (32512 to 32516, 32642 to 32646, 32648 to 32651): `return TRUE;`, nothing
  changes, last error is left alone, no log line. Any other value, NULL among them:
  `RT_UNIMPLEMENTED(ctx, "DestroyCursor of %p, which is not a system cursor: no other cursor
  can exist here", hCursor);` (every function that makes one stops). Status `partial`;
  `RT-TRAPS: not a system cursor`.
- Tests: after `SetLastError(0xdeadbeef)`, `DestroyCursor(RT_HCURSOR_SYSTEM(IDC_ARROW))` is TRUE
  and last error is still 0xdeadbeef; the same for `IDC_SIZEALL` (32646) and `IDC_HELP` (32651),
  the ends of the ranges; `rt_cursor_set(RT_HCURSOR_SYSTEM(IDC_ARROW))` afterwards does not stop
  (the cursor is still one; include `rt/window.h`); the stops for `(HCURSOR)0x1234` and for
  `RT_HCURSOR_SYSTEM(32647)`, a number between the ranges.""",
        stage="E"),
    row("cursor-pos", G_CUR, ["GetCursorPos", "SetCursorPos"],
        ["BOOL GetCursorPos(LPPOINT lpPoint)", "BOOL SetCursorPos(INT X, INT Y)"],
        "the position guest code last set; the middle of the screen at first", WIN_ERR + """\
- `return rt_cursor_get_pos(lpPoint);` and `return rt_cursor_set_pos(X, Y);`.
- Tests: {960, 540} at first; set {100, 200} then get; {5000, -7} is clamped to {1919, 0};
  `GetCursorPos(NULL)` is FALSE with `ERROR_NOACCESS` (998: Wine `user32/tests/input.c`,
  `test_SetCursorPos`; review D10).""",
        waits=[INPUT], header=WH, core=["rt_cursor_get_pos", "rt_cursor_set_pos"]),
    row("clip-cursor", G_CUR, ["ClipCursor", "GetClipCursor"],
        ["BOOL ClipCursor(LPCRECT lpRect)", "BOOL GetClipCursor(LPRECT lpRect)"],
        "the clip rectangle, stored; the whole screen at first", WIN_ERR + """\
- `return rt_cursor_clip(lpRect);` and `return rt_cursor_get_clip(lpRect);`.
- Tests: {0, 0, 1920, 1080} at first; a clip of {100, 100, 200, 200} is returned and moves the
  cursor (`rt_cursor_get_pos`) from {960, 540} to {199, 199}; with that clip set, the inverted
  rectangle {50, 50, 49, 49} is FALSE with `ERROR_ACCESS_DENIED` and `GetClipCursor` still gives
  {100, 100, 200, 200} (Wine `user32/tests/input.c`, `test_ClipCursor_desktop`; review D11); NULL
  restores the screen; the stop for the empty rectangle {50, 50, 50, 90}.""",
        waits=[INPUT], header=WH, core=["rt_cursor_clip", "rt_cursor_get_clip", "rt_cursor_get_pos"]),

    # ---- keyboard, mouse and raw input ------------------------------------------------------------
    row("key-state", G_KEY, ["GetKeyState"], ["SHORT GetKeyState(INT nVirtKey)"],
        "0: every key is up and not toggled", WIN_ERR + """\
- `return rt_key_state(nVirtKey);`.
- Tests: 0 for keys 0, 0x10 (`VK_SHIFT`), 0x14 (`VK_CAPITAL`), 0x41 and 255; last error left
  alone; the stop for key 256.""",
        waits=[INPUT], header=WH, core=["rt_key_state"]),
    row("keyboard-layout", G_KEY, ["GetKeyboardLayout", "GetKeyboardLayoutList"],
        ["HKL GetKeyboardLayout(DWORD idThread)", "INT GetKeyboardLayoutList(INT nBuff, HKL *lpList)"],
        "US English, the one layout", FIXED + """\
- The machine has one keyboard layout, US English, `RT_HKL_US` (`rt/window.h`; `docs/RUNTIME.md`
  8.4: no locale but en-US).
- `GetKeyboardLayout`: `idThread` 0 or the calling thread's id (`RT_THREAD(ctx)->id`): `return
  RT_HKL_US;`. Another id is `RT_UNIMPLEMENTED` (whether that thread exists would have to be
  asked). Status `partial`.
- `GetKeyboardLayoutList`: `nBuff == 0` returns 1 (the number of layouts; the list is not
  touched). `nBuff > 0` with a list: store `RT_HKL_US` in `lpList[0]`, return 1. `nBuff > 0` with a
  NULL list, or `nBuff < 0`: `RT_UNIMPLEMENTED`. Status `partial`. Last error is left alone.
- Tests: the handle `(HKL)0x04090409` for 0 and for `CALL(kernel32, GetCurrentThreadId)`; the
  count; a list of 2 preset to 0xA5 has the layout first and the second entry untouched; the
  stops.""",
        **METRICS),
    row("keyboard-layout-set", G_KEY, ["ActivateKeyboardLayout", "LoadKeyboardLayoutA"],
        ["HKL ActivateKeyboardLayout(HKL hkl, UINT Flags)", "HKL LoadKeyboardLayoutA(LPCSTR pwszKLID, UINT Flags)"],
        "the one layout is already loaded and active; another layout stops", FIXED + """\
- `ActivateKeyboardLayout`: `hkl == RT_HKL_US`, `(HKL)1` (`HKL_NEXT`) or `(HKL)0` (`HKL_PREV`):
  the layout stays what it is: `return RT_HKL_US;` (documented: the previous layout). Any other
  handle is `RT_UNIMPLEMENTED`. Status `partial`.
- `LoadKeyboardLayoutA`: the name "00000409" (compare the eight characters exactly): `return
  RT_HKL_US;`. Any other name, and NULL, is `RT_UNIMPLEMENTED` with the name. Status `partial`.
- Both leave last error alone. Tests: the three accepted handles; the accepted name with flags
  0 and 1; the stops for `(HKL)0x04070407` and for "00000407" and NULL.""",
        **METRICS),
    row("map-virtual-key", G_KEY, ["MapVirtualKeyExA"], ["UINT MapVirtualKeyExA(UINT uCode, UINT uMapType, HKL dwhkl)"],
        "the US layout's table of keys, scan codes and characters", WIN_ERR + """\
- `return rt_key_map(uCode, uMapType, dwhkl);`.
- Tests take their expected values from the piece's own table source (Wine's, named in
  `core/win_keymap.c`), not from memory: at least `VK_ESCAPE` (0x1B) to scan code 1 and back,
  'A' (0x41) to 0x1E, and map type 2 of 0x41 giving 'A'; a code with no translation gives 0;
  the stop for map type 9.""",
        waits=[KEYMAP], header=WH, core=["rt_key_map"]),
    row("raw-input", G_KEY, ["GetRawInputDeviceList", "GetRawInputDeviceInfoA"],
        ["UINT GetRawInputDeviceList(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize)",
         "UINT GetRawInputDeviceInfoA(HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize)"],
        "no raw-input device is attached: an empty list", FIXED + """\
- Status `absent`, signed off in the contract: no input device is attached to this machine
  before Gate 4 (`docs/RUNTIME.md` 9.2 names "no HID devices"). Each logs once at debug level:
  `rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "user32.dll!<Name>", "<Name>: no raw-input device is
  attached");`.
- `GetRawInputDeviceList`, in this order (Wine `user32/tests/input.c`,
  `test_GetRawInputDeviceList`; review V20, C8): `cbSize != 16` (`sizeof(RAWINPUTDEVICELIST)`):
  `return FAIL(ctx, ERROR_INVALID_PARAMETER, (UINT)-1);`, whatever the count pointer. Then
  `puiNumDevices` NULL: `return FAIL(ctx, ERROR_NOACCESS, (UINT)-1);` (998). Then a NULL list
  (the size query): `*puiNumDevices = 0; return 0;`. Then a list: `return 0;` and
  `*puiNumDevices` is NOT written (Microsoft's page: with a buffer large enough for the
  devices, here none, "the value is unchanged"); the list is not written.
- `GetRawInputDeviceInfoA`: no device handle was ever handed out, so every handle is invalid.
  `pcbSize` NULL: `return FAIL(ctx, ERROR_NOACCESS, (UINT)-1);`. Otherwise `return FAIL(ctx,
  ERROR_INVALID_HANDLE, (UINT)-1);` and `*pcbSize` is not written (the same test asserts both
  for the W form). Nothing else is read or written.
- Tests: the size query (NULL list) gives 0 and a count of 0 from a preset 77; with a list
  preset to 0xA5 and a count preset to 5: 0, the count still 5, the list untouched; `cbSize` 8
  with a NULL count: -1 with 87; `cbSize` 16 with a NULL count: -1 with 998;
  `GetRawInputDeviceInfoA` for `(HANDLE)0x44` with every pointer NULL: -1 with 998; with a size
  preset to 77: -1 with 6 and the size still 77; the log lines.""",
        **METRICS),
    row("hooks", G_KEY, ["SetWindowsHookExA", "UnhookWindowsHookEx"],
        ["HHOOK SetWindowsHookExA(INT idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId)",
         "BOOL UnhookWindowsHookEx(HHOOK hhk)"],
        "a record for a keyboard or mouse hook, never called (no input exists); other hook types stop", WIN_ERR + """\
- `return rt_hook_set(idHook, lpfn, hmod, dwThreadId);` and `return rt_hook_remove(hhk);`.
- Tests (`tid` is the calling thread's id, `mod` any non-NULL value, `proc` a `GUEST_FN`
  address): `WH_KEYBOARD_LL` (13) with `(proc, NULL, 0)` and `WH_MOUSE` (7) with
  `(proc, NULL, tid)` give two distinct handles; unhook is TRUE once and FALSE with
  `ERROR_INVALID_HOOK_HANDLE` the second time; type 99 fails with `ERROR_INVALID_HOOK_FILTER`.
  The codes Wine's `user32/tests/msg.c` (`test_set_hook`) asserts: `WH_KEYBOARD` (2) with
  `(0, NULL, tid)` is NULL with 1427 (`ERROR_INVALID_FILTER_PROC`); `WH_MOUSE_LL` (14) with
  `(proc, mod, tid)` is NULL with 1429 (`ERROR_GLOBAL_ONLY_HOOK`); `WH_KEYBOARD` with
  `(proc, NULL, 0)` is NULL with 1428 (`ERROR_HOOK_NEEDS_HMOD`), and so is `WH_CBT` (5) with
  `(proc, NULL, 0)`, the case the test itself has (`docs/reviews/win-behaviour-check-5.md`, Z2);
  `WH_KEYBOARD` with `(proc, NULL, tid)` succeeds. The stop for `WH_CBT` with `(proc, mod, 0)`.""",
        waits=[HOOK], header=WH, core=["rt_hook_set", "rt_hook_remove"]),

    # ---- clipboard -----------------------------------------------------------------------------------
    row("clipboard-open", G_CLIP, ["OpenClipboard", "CloseClipboard"],
        ["BOOL OpenClipboard(HWND hWndNewOwner)", "BOOL CloseClipboard(void)"],
        "this process's own clipboard: opened and closed", WIN_ERR + """\
- `return rt_clipboard_open(hWndNewOwner);` and `return rt_clipboard_close();`.
- Tests: close before open fails with `ERROR_CLIPBOARD_NOT_OPEN`; open with NULL, open with NULL
  again (TRUE: the same owner; a different owner would fail, review D20), close once, and a
  second close fails; an owner that is no window fails with `ERROR_INVALID_WINDOW_HANDLE`.""",
        waits=[CLIP], header=WH, core=["rt_clipboard_open", "rt_clipboard_close"]),
    row("clipboard-data", G_CLIP, ["GetClipboardData", "SetClipboardData"],
        ["HANDLE GetClipboardData(UINT uFormat)", "HANDLE SetClipboardData(UINT uFormat, HANDLE hMem)"],
        "empty at first; holds what guest code put there", WIN_ERR + """\
- `return rt_clipboard_get(uFormat);` and `return rt_clipboard_set(uFormat, hMem);`.
- Tests (open with `rt_clipboard_open(NULL)` first): get of `CF_UNICODETEXT` (13) on the empty
  clipboard is NULL with `ERROR_NOT_FOUND` (1168: Wine `user32/tests/clipboard.c`,
  `test_synthesized`; review D19); set `(HANDLE)0x5000` for format 0xC123 returns it and get
  returns it; set for format 0 is NULL with `ERROR_CLIPBOARD_NOT_OPEN` although the clipboard is
  open (`test_RegisterClipboardFormatA`; review D21); both without an open clipboard fail with
  `ERROR_CLIPBOARD_NOT_OPEN`; the stops for a NULL handle and for get of `CF_TEXT` (1) when only
  13 is set.""",
        waits=[CLIP], header=WH, core=["rt_clipboard_get", "rt_clipboard_set", "rt_clipboard_open", "rt_clipboard_close"]),
    row("clipboard-empty", G_CLIP, ["EmptyClipboard"], ["BOOL EmptyClipboard(void)"],
        "every format is forgotten", WIN_ERR + """\
- `return rt_clipboard_empty();`.
- Tests: without an open clipboard it fails with `ERROR_CLIPBOARD_NOT_OPEN`; after a set, empty
  is TRUE and `rt_clipboard_get` of that format is NULL.""",
        waits=[CLIP], header=WH, core=["rt_clipboard_empty", "rt_clipboard_open", "rt_clipboard_set", "rt_clipboard_get"]),

    # ---- the input method -------------------------------------------------------------------------
    row("imm-context", G_IME, ["ImmGetContext", "ImmReleaseContext"],
        ["HIMC ImmGetContext(HWND hWnd)", "BOOL ImmReleaseContext(HWND hWnd, HIMC hIMC)"],
        "no input context: NULL; releasing is TRUE", ABSENT_IME + """\
- `ImmGetContext`: `return NULL;` for every window; last error left alone. (This is the
  contract's choice, not Windows': there a window of a thread with the input-method manager
  enabled has a context.) `ImmReleaseContext`: `return TRUE;` for every argument, the `HIMC`
  included, last error left alone (Wine and ReactOS return TRUE unconditionally, from memory;
  not settled, review N8): the shared rule for a non-NULL `HIMC` does not apply to it.""",
        lib="imm32", **METRICS),
    row("imm-composition", G_IME, ["ImmGetCompositionStringW"],
        ["LONG ImmGetCompositionStringW(HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen)"],
        "0 bytes: there is no composition", ABSENT_IME + """\
- 0 by the shared rule: a NULL `HIMC` leaves last error alone, a non-NULL one sets
  `ERROR_INVALID_HANDLE` (both asserted by `test_InvalidIMC` for the A form).
- Tests: `ImmGetCompositionStringW(NULL, 8, buf, 16)` is 0 with last error still 0xdeadbeef;
  `ImmGetCompositionStringW((HIMC)0xdeadcafe, 8, NULL, 0)` is 0 with last error 6.""",
        lib="imm32", **METRICS),
    row("imm-candidates", G_IME, ["ImmGetCandidateListW", "ImmGetCandidateListCountW"],
        ["DWORD ImmGetCandidateListW(HIMC hIMC, DWORD deIndex, LPVOID lpCandList, DWORD dwBufLen)",
         "DWORD ImmGetCandidateListCountW(HIMC hIMC, LPDWORD lpdwListCount)"],
        "0: there is no candidate list", ABSENT_IME + """\
- Both 0 by the shared rule (NULL: last error left alone; non-NULL: `ERROR_INVALID_HANDLE`;
  `test_InvalidIMC`). `*lpdwListCount` is not written.""",
        lib="imm32", **METRICS),
    row("imm-conversion", G_IME, ["ImmGetConversionStatus", "ImmSetConversionStatus"],
        ["BOOL ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)",
         "BOOL ImmSetConversionStatus(HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence)"],
        "FALSE: there is no context to ask or change", ABSENT_IME + """\
- `ImmGetConversionStatus`: FALSE by the shared rule; neither DWORD is written. (Its last
  error for a NULL `HIMC` was not in the tests read: left alone, from memory; review N8.)
- `ImmSetConversionStatus`: FALSE with `ERROR_INVALID_HANDLE` for a NULL `HIMC` as well as for a
  non-NULL one (`test_InvalidIMC` asserts both): `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);`
  for every argument.
- Tests: after `SetLastError(0xdeadbeef)`, `ImmSetConversionStatus(NULL, 0, 0)` is FALSE with
  last error 6; `ImmGetConversionStatus(NULL, &a, &b)` FALSE, 0xdeadbeef, both DWORDs
  untouched; each with `(HIMC)0xdeadcafe`: FALSE, 6.""",
        lib="imm32", **METRICS),
    row("imm-property", G_IME, ["ImmGetProperty", "ImmIsUIMessageA"],
        ["DWORD ImmGetProperty(HKL hKL, DWORD fdwIndex)", "BOOL ImmIsUIMessageA(HWND hWndIME, UINT msg, WPARAM wParam, LPARAM lParam)"],
        "0: the keyboard layout has no input method; which messages are the input method's, and a stop "
        "where one would have to be sent to a window", ABSENT_IME + """\
- `ImmGetProperty`: `return 0;` for every layout and index (from memory; not settled, review
  N8: Wine's test asks only the current layout). It takes no `HIMC`.
- `ImmIsUIMessageA` takes no `HIMC` either; status `partial`. The eight user-interface messages
  are `WM_IME_STARTCOMPOSITION` 0x10D, `WM_IME_ENDCOMPOSITION` 0x10E, `WM_IME_COMPOSITION` 0x10F,
  `WM_IME_SETCONTEXT` 0x281, `WM_IME_NOTIFY` 0x282, `WM_IME_COMPOSITIONFULL` 0x284,
  `WM_IME_SELECT` 0x285 and 0x287 (`WM_IME_SYSTEM`) (Wine `imm32/tests/imm32.c`,
  `test_ImmIsUIMessage`; review V19, C7). Any other message: `return FALSE;`. One of the eight
  with `hWndIME == NULL`: `return TRUE;` (Windows sends nothing for a NULL window: the same
  test). One of the eight with a window: `RT_UNIMPLEMENTED(ctx, "an input-method message 0x%x
  for window %p: it would have to be sent to the window", msg, hWndIME)` (Windows sends the
  message to it, and nothing here produces such a message).
- Tests of the second: FALSE for `WM_NULL`, `WM_CHAR` (0x102), `WM_USER`, 0x283
  (`WM_IME_CONTROL`), 0x286 (`WM_IME_CHAR`); TRUE for each of the eight with a NULL window;
  the stop for 0x10F and 0x287 with `(HWND)0x40000FF0`.""",
        lib="imm32", **METRICS),
    row("imm-notify", G_IME, ["ImmNotifyIME", "ImmSetCandidateWindow"],
        ["BOOL ImmNotifyIME(HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue)",
         "BOOL ImmSetCandidateWindow(HIMC hIMC, LPCANDIDATEFORM lpCandidate)"],
        "FALSE: there is no context to notify or to place", ABSENT_IME + """\
- `ImmNotifyIME`: a non-NULL `HIMC`: FALSE with `ERROR_INVALID_HANDLE`; a NULL `HIMC`: FALSE
  and last error SET TO 0 (`SetLastError(ERROR_SUCCESS)`; Wine `imm32/tests/imm32.c`,
  `test_ImmNotifyIME` asserts both).
- `ImmSetCandidateWindow`: FALSE by the shared rule; the `CANDIDATEFORM` is not read. (Its last
  error for a NULL `HIMC` was not in the tests read: left alone, from memory; review N8.)
- Tests: after `SetLastError(0xdeadbeef)`, `ImmNotifyIME(NULL, 0x15, 4, 0)` (`NI_COMPOSITIONSTR`,
  `CPS_CANCEL`) is FALSE with last error 0; with `(HIMC)0xdeadcafe` FALSE with 6;
  `ImmSetCandidateWindow` likewise FALSE with 0xdeadbeef kept and with 6.""",
        lib="imm32", **METRICS),

    # ---- the rest ------------------------------------------------------------------------------------
    row("rect-set", G_REST, ["SetRect"], ["BOOL SetRect(LPRECT lprc, INT xLeft, INT yTop, INT xRight, INT yBottom)"],
        "the four values stored (arithmetic: no screen is involved)", """\
- Store the four values in the RECT and return TRUE. A NULL RECT returns FALSE and sets no
  last error (Wine, from memory; the documentation only says zero on failure). Status `full`,
  `RT-TRAPS: none`.
- Tests: {1, -2, 3, 4} arrives member by member, the 8 bytes after the RECT stay 0xA5; NULL is
  FALSE with last error left alone.""",
        model="IntersectRect", model_lib="user32"),
    row("rect-ops", G_REST, ["IntersectRect", "UnionRect"],
        ["BOOL IntersectRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2)",
         "BOOL UnionRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2)"],
        "the intersection and the bounding rectangle (arithmetic: no screen is involved)", """\
- That "empty" follows `IsRectEmpty`'s rule is confirmed (Wine `user32/tests/uitools.c`,
  `test_IsRectEmpty`; review V55 of `contracts-check-2.md`). Not settled (reviews N6, S5),
  written as the contract decided: that a NULL pointer returns FALSE is from memory of Wine;
  the two pages say the destination "cannot be NULL".
- A rectangle is EMPTY when `left >= right` or `top >= bottom` (documented in `IsRectEmpty`).
- `IntersectRect`: any pointer NULL returns FALSE. If either source is empty, or they do not
  overlap (`max(left) >= min(right)` or `max(top) >= min(bottom)`), the destination becomes
  {0, 0, 0, 0} and the result is FALSE (documented: an empty rectangle with all coordinates
  zero). Otherwise the destination is {max of the lefts, max of the tops, min of the rights,
  min of the bottoms} and the result TRUE. The destination may be one of the sources: compute
  into a local first.
- `UnionRect`: any pointer NULL returns FALSE. An empty source is ignored (documented): both
  empty gives {0, 0, 0, 0} and FALSE; one empty gives a copy of the other and TRUE; else
  {min of the lefts, min of the tops, max of the rights, max of the bottoms} and TRUE.
- Neither sets last error. Status `full`, `RT-TRAPS: none`.
- Tests: an overlap; rectangles that only touch at an edge (no intersection); one inside the
  other; an empty source in each position; the destination aliasing each source; NULL in each
  position with a preset destination untouched; last error left alone.""",
        **METRICS),
]

# Written by the contract unit itself (runtime/shims/user32/, core/win_msgbox.c).
BUILT = {
    "MessageBoxA": (G_MSG, "ends the run: caption and text in the log (channel trap) and in the stop report"),
    "MessageBoxW": (G_MSG, "ends the run: caption and text in the log (channel trap) and in the stop report"),
}

DRAW = "puts pixels somewhere; nothing draws before Gate 4"
ICON = "needs cursor and icon images (resources or files); they arrive with drawing, at Gate 4"
# Functions that stay stops (the import's trap stub; no shim, no row), each with the reason.
STOPS = {
    "BeginPaint": (G_WIN, "painting, which is Gate 4's; and WM_PAINT, its only legitimate caller, never arrives: the "
                          "window core sends no paint message, to a shown window either (rt/window.h, rt_window_show: "
                          "\"there is no screen, and a record has no update region\"). The recording has one WM_PAINT, "
                          "which the engine's procedure answers with BeginPaint and EndPaint at 4.646 s "
                          "(engine+0x1cf07e, +0x1cf098): a known difference, review census-rows 1.7"),
    "EndPaint": (G_WIN, "as BeginPaint"),
    "FlashWindow": (G_WIN, "flashes a visible window's caption and taskbar button; neither exists before Gate 4"),
    "SetTimer": (G_MSG, "WM_TIMER generation is not designed (it would be the queue's first message that nobody "
                        "posts); one importer, vgui2; decide when a caller appears"),
    "CallNextHookEx": (G_KEY, "only a hook procedure may call it, and none is ever called before Gate 4 "
                              "(the hooks that are accepted are input hooks)"),
    "EnumDisplayDevicesW": (G_DESK, "the adapter's name and vendor id must agree with what the Direct3D device "
                                    "reports, which is the unit design-null-device's decision"),
    "LoadImageA": (G_CUR, ICON),
    "LoadCursorFromFileA": (G_CUR, ICON),
    "DestroyIcon": (G_CUR, ICON + "; no icon exists that it could destroy"),
    "CopyIcon": (G_CUR, ICON),
    "CreateIconIndirect": (G_CUR, ICON),
    "GetIconInfo": (G_CUR, ICON),
    "DrawIconEx": (G_CUR, DRAW),
    "DrawTextA": (G_GDI, DRAW),
    "BitBlt": (G_GDI, DRAW),
    "StretchBlt": (G_GDI, DRAW),
    "ExtTextOutA": (G_GDI, DRAW),
    "ExtTextOutW": (G_GDI, DRAW),
    "Polygon": (G_GDI, DRAW),
    "Polyline": (G_GDI, DRAW),
    "Rectangle": (G_GDI, DRAW),
    "SetPixel": (G_GDI, DRAW),
    "GetPixel": (G_GDI, "reads a pixel; no bitmap but a DIB section has pixels before Gate 4"),
    "SetDIBits": (G_GDI, DRAW),
    "GetDIBits": (G_GDI, "copies pixels out; its header-only form (which format has the screen?) could be answered "
                         "once someone has checked what Windows reports for a 32-bit screen bitmap (BI_RGB or "
                         "BI_BITFIELDS)"),
    "AddFontMemResourceEx": (G_GDI, "must parse a font file to count its fonts; fonts arrive with text drawing, at Gate 4"),
    "RemoveFontMemResourceEx": (G_GDI, "no font resource can exist (AddFontMemResourceEx stops)"),
    "RemoveFontResourceA": (G_GDI, "fonts arrive with text drawing, at Gate 4"),
}

# BEGIN IMPORTERS (generated: tools/swarm/shim_rows_window.py --importers; do not edit)
IMPORTERS = {
    'ActivateKeyboardLayout': ('user32', ['vgui2']),
    'AddFontMemResourceEx': ('gdi32', ['vgui2']),
    'AdjustWindowRectEx': ('user32', ['tier0', 'engine']),
    'AllowSetForegroundWindow': ('user32', ['client', 'server']),
    'BeginPaint': ('user32', ['engine', 'bink2w64', 'vgui2']),
    'BitBlt': ('gdi32', ['engine', 'vgui2']),
    'CallNextHookEx': ('user32', ['tier0']),
    'CallWindowProcA': ('user32', ['engine']),
    'CallWindowProcW': ('user32', ['inputsystem']),
    'ChangeDisplaySettingsA': ('user32', ['bink2w64']),
    'ClientToScreen': ('user32', ['tier0', 'engine', 'bink2w64', 'inputsystem', 'vgui2']),
    'ClipCursor': ('user32', ['inputsystem']),
    'CloseClipboard': ('user32', ['engine', 'vgui2']),
    'CopyIcon': ('user32', ['vgui2']),
    'CreateBitmap': ('gdi32', ['vgui2']),
    'CreateCompatibleBitmap': ('gdi32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vgui2']),
    'CreateCompatibleDC': ('gdi32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vgui2']),
    'CreateDCW': ('gdi32', ['amd_ags_x64']),
    'CreateDIBSection': ('gdi32', ['engine', 'bink2w64', 'vgui2']),
    'CreateFontA': ('gdi32', ['engine']),
    'CreateIconIndirect': ('user32', ['vgui2']),
    'CreatePen': ('gdi32', ['vgui2']),
    'CreateRectRgn': ('gdi32', ['vgui2']),
    'CreateSolidBrush': ('gdi32', ['engine', 'vgui2']),
    'CreateWindowExA': ('user32', ['tier0', 'bink2w64', 'vgui2']),
    'CreateWindowExW': ('user32', ['engine']),
    'DefWindowProcA': ('user32', ['tier0', 'bink2w64', 'vgui2']),
    'DefWindowProcW': ('user32', ['engine']),
    'DeleteDC': ('gdi32', ['engine', 'bink2w64', 'amd_ags_x64', 'materialsystem_dx11', 'vgui2']),
    'DeleteObject': ('gdi32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vgui2']),
    'DestroyCursor': ('user32', ['inputsystem']),
    'DestroyIcon': ('user32', ['vguimatsurface', 'vgui2']),
    'DestroyWindow': ('user32', ['engine', 'bink2w64', 'vgui2']),
    'DispatchMessageA': ('user32', ['engine', 'inputsystem', 'vgui2']),
    'DispatchMessageW': ('user32', ['engine', 'materialsystem_dx11']),
    'DrawIconEx': ('user32', ['vgui2']),
    'DrawTextA': ('user32', ['engine']),
    'EmptyClipboard': ('user32', ['engine', 'vgui2']),
    'EndPaint': ('user32', ['engine', 'bink2w64', 'vgui2']),
    'EnumDisplayDevicesW': ('user32', ['amd_ags_x64']),
    'EnumDisplayMonitors': ('user32', ['materialsystem_dx11', 'vgui2']),
    'EnumWindows': ('user32', ['tier0', 'client']),
    'ExtEscape': ('gdi32', ['amd_ags_x64']),
    'ExtTextOutA': ('gdi32', ['vgui2']),
    'ExtTextOutW': ('gdi32', ['vgui2']),
    'FindWindowA': ('user32', ['tier0', 'launcher', 'engine', 'client', 'server']),
    'FlashWindow': ('user32', ['vgui2']),
    'GetActiveWindow': ('user32', ['bink2w64', 'mileswin64', 'client']),
    'GetBkColor': ('gdi32', ['vgui2']),
    'GetClassLongPtrA': ('user32', ['bink2w64']),
    'GetClientRect': ('user32', ['tier0', 'engine', 'bink2w64', 'materialsystem_dx11']),
    'GetClipboardData': ('user32', ['vgui2']),
    'GetClipCursor': ('user32', ['inputsystem']),
    'GetCursorPos': ('user32', ['bink2w64', 'inputsystem', 'vgui2']),
    'GetDC': ('user32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vguimatsurface', 'vgui2']),
    'GetDesktopWindow': ('user32', ['engine', 'bink2w64', 'vgui2']),
    'GetDeviceCaps': ('gdi32', ['engine', 'bink2w64', 'vgui2']),
    'GetDeviceGammaRamp': ('gdi32', ['materialsystem_dx11']),
    'GetDIBits': ('gdi32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vguimatsurface']),
    'GetDlgItem': ('user32', ['tier0']),
    'GetFocus': ('user32', ['inputsystem', 'vgui2']),
    'GetForegroundWindow': ('user32', ['engine', 'inputsystem', 'mileswin64']),
    'GetIconInfo': ('user32', ['vguimatsurface', 'vgui2']),
    'GetKeyboardLayout': ('user32', ['inputsystem', 'vgui2']),
    'GetKeyboardLayoutList': ('user32', ['vgui2']),
    'GetKeyState': ('user32', ['engine', 'inputsystem', 'vgui2']),
    'GetMessageA': ('user32', ['vgui2']),
    'GetMessageTime': ('user32', ['inputsystem']),
    'GetObjectA': ('gdi32', ['engine', 'vguimatsurface', 'vgui2']),
    'GetObjectW': ('gdi32', ['engine']),
    'GetPixel': ('gdi32', ['bink2w64']),
    'GetProcessWindowStation': ('user32', ['engine']),
    'GetRawInputDeviceInfoA': ('user32', ['inputsystem']),
    'GetRawInputDeviceList': ('user32', ['inputsystem']),
    'GetShellWindow': ('user32', ['tier0']),
    'GetSystemMetrics': ('user32', ['tier0', 'engine', 'bink2w64']),
    'GetTextColor': ('gdi32', ['vgui2']),
    'GetTopWindow': ('user32', ['bink2w64', 'mileswin64']),
    'GetUserObjectInformationW': ('user32', ['engine']),
    'GetWindow': ('user32', ['engine', 'bink2w64', 'mileswin64']),
    'GetWindowLongA': ('user32', ['bink2w64', 'vgui2', 'mileswin64']),
    'GetWindowLongPtrA': ('user32', ['bink2w64', 'vgui2', 'mileswin64']),
    'GetWindowLongPtrW': ('user32', ['engine', 'inputsystem']),
    'GetWindowRect': ('user32', ['tier0', 'engine', 'bink2w64', 'inputsystem', 'vgui2']),
    'GetWindowTextA': ('user32', ['client']),
    'GetWindowThreadProcessId': ('user32', ['tier0', 'bink2w64', 'mileswin64', 'client', 'server']),
    'ImmGetCandidateListCountW': ('imm32', ['vgui2']),
    'ImmGetCandidateListW': ('imm32', ['vgui2']),
    'ImmGetCompositionStringW': ('imm32', ['vgui2']),
    'ImmGetContext': ('imm32', ['vgui2']),
    'ImmGetConversionStatus': ('imm32', ['vgui2']),
    'ImmGetProperty': ('imm32', ['vgui2']),
    'ImmIsUIMessageA': ('imm32', ['vgui2']),
    'ImmNotifyIME': ('imm32', ['vgui2']),
    'ImmReleaseContext': ('imm32', ['vgui2']),
    'ImmSetCandidateWindow': ('imm32', ['vgui2']),
    'ImmSetConversionStatus': ('imm32', ['vgui2']),
    'IntersectRect': ('user32', ['engine']),
    'InvalidateRect': ('user32', ['engine', 'vgui2']),
    'IsIconic': ('user32', ['tier0', 'engine', 'bink2w64', 'inputsystem', 'materialsystem_dx11', 'vgui2']),
    'IsWindowVisible': ('user32', ['tier0', 'engine', 'bink2w64']),
    'LoadCursorA': ('user32', ['engine', 'inputsystem', 'vgui2']),
    'LoadCursorFromFileA': ('user32', ['inputsystem']),
    'LoadIconA': ('user32', ['engine', 'vgui2']),
    'LoadImageA': ('user32', ['engine', 'vgui2']),
    'LoadKeyboardLayoutA': ('user32', ['inputsystem']),
    'MapVirtualKeyExA': ('user32', ['inputsystem']),
    'MessageBoxA': ('user32', ['tier0', 'launcher', 'engine', 'materialsystem_dx11', 'server']),
    'MessageBoxW': ('user32', ['engine']),
    'MoveToEx': ('gdi32', ['vgui2']),
    'MsgWaitForMultipleObjects': ('user32', ['inputsystem']),
    'OpenClipboard': ('user32', ['engine', 'vgui2']),
    'PeekMessageA': ('user32', ['bink2w64', 'inputsystem', 'vgui2']),
    'PeekMessageW': ('user32', ['engine', 'materialsystem_dx11']),
    'Polygon': ('gdi32', ['vgui2']),
    'Polyline': ('gdi32', ['vgui2']),
    'PostMessageA': ('user32', ['client']),
    'Rectangle': ('gdi32', ['engine']),
    'RegisterClassA': ('user32', ['bink2w64', 'vgui2']),
    'RegisterClassExA': ('user32', ['tier0']),
    'RegisterClassExW': ('user32', ['engine']),
    'RegisterWindowMessageA': ('user32', ['inputsystem', 'vgui2', 'client']),
    'ReleaseCapture': ('user32', ['inputsystem', 'vgui2']),
    'ReleaseDC': ('user32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vguimatsurface', 'vgui2']),
    'RemoveFontMemResourceEx': ('gdi32', ['vguimatsurface']),
    'RemoveFontResourceA': ('gdi32', ['vgui2']),
    'ScreenToClient': ('user32', ['tier0', 'bink2w64', 'inputsystem']),
    'SelectObject': ('gdi32', ['engine', 'bink2w64', 'materialsystem_dx11', 'vgui2']),
    'SendMessageA': ('user32', ['launcher', 'engine', 'inputsystem', 'server']),
    'SetActiveWindow': ('user32', ['vgui2']),
    'SetBkColor': ('gdi32', ['vgui2']),
    'SetBkMode': ('gdi32', ['engine', 'vgui2']),
    'SetCapture': ('user32', ['inputsystem', 'vgui2']),
    'SetClipboardData': ('user32', ['engine', 'vgui2']),
    'SetCursor': ('user32', ['bink2w64', 'inputsystem', 'vgui2']),
    'SetCursorPos': ('user32', ['inputsystem', 'vgui2']),
    'SetDIBits': ('gdi32', ['engine']),
    'SetFocus': ('user32', ['vgui2']),
    'SetForegroundWindow': ('user32', ['engine', 'vgui2', 'client', 'server']),
    'SetPixel': ('gdi32', ['engine', 'bink2w64']),
    'SetRect': ('user32', ['engine']),
    'SetRectRgn': ('gdi32', ['vgui2']),
    'SetStretchBltMode': ('gdi32', ['bink2w64', 'materialsystem_dx11']),
    'SetTextAlign': ('gdi32', ['vgui2']),
    'SetTextColor': ('gdi32', ['engine', 'vgui2']),
    'SetTimer': ('user32', ['vgui2']),
    'SetViewportOrgEx': ('gdi32', ['vgui2']),
    'SetWindowLongA': ('user32', ['vgui2']),
    'SetWindowLongPtrA': ('user32', ['vgui2']),
    'SetWindowLongPtrW': ('user32', ['engine', 'inputsystem']),
    'SetWindowPos': ('user32', ['tier0', 'engine', 'vgui2']),
    'SetWindowsHookExA': ('user32', ['tier0']),
    'SetWindowTextA': ('user32', ['tier0', 'vgui2']),
    'SetWindowTextW': ('user32', ['vgui2']),
    'ShowCursor': ('user32', ['engine', 'bink2w64']),
    'ShowWindow': ('user32', ['engine', 'materialsystem_dx11', 'vgui2']),
    'StretchBlt': ('gdi32', ['bink2w64', 'materialsystem_dx11', 'vgui2']),
    'SystemParametersInfoA': ('user32', ['vgui2']),
    'TranslateAcceleratorA': ('user32', ['materialsystem_dx11']),
    'TranslateMessage': ('user32', ['engine', 'inputsystem', 'materialsystem_dx11', 'vgui2']),
    'UnhookWindowsHookEx': ('user32', ['tier0']),
    'UnionRect': ('user32', ['vgui2']),
    'UnregisterClassA': ('user32', ['bink2w64', 'vgui2']),
    'UnregisterClassW': ('user32', ['engine']),
    'UpdateWindow': ('user32', ['engine']),
    'ValidateRect': ('user32', ['vgui2']),
    'WindowFromPoint': ('user32', ['vgui2']),
}
# END IMPORTERS

ORDER = ["tier0", "vstdlib", "launcher", "inputsystem", "vgui2", "materialsystem_dx11", "engine"]


def rank(t):
    """A row's place in the order: the earliest of ORDER that imports one of its functions."""
    best = len(ORDER)
    for m in t["members"]:
        for who in IMPORTERS.get(m, ("?", []))[1]:
            if who in ORDER:
                best = min(best, ORDER.index(who))
    return best


WINDOW_TASKS.sort(key=rank)             # stable: within one importer the order above stays

# The two rows the contract unit proved (handoff, section 6): their shims are written and are
# the models other rows name, so no unit is made for them.
for _t in WINDOW_TASKS:
    if _t["id"] in ("win-system-metrics", "win-rect-ops"):
        _t["hold"] = "written by rt-contract-window as the proof of the rows"
    # Written by unit rt-window-core-1 with the piece win-class, because the load of vgui2
    # waited for them (docs/handoff/rt-window-core-1.md); that unit also rewrote LoadIconA of
    # row win-load-cursor, whose unit was done before the icon decision.
    if _t["id"] in ("win-register-class-a", "win-register-message"):
        _t["hold"] = "written by rt-window-core-1 with the piece win-class"
    # Written by unit rt-win-queue with the piece win-queue (docs/handoff/rt-win-queue.md):
    # the render thread's first act is PeekMessageW, and the others sit on the same piece.
    if _t["id"] in ("win-peek-message", "win-get-message", "win-post-message", "win-msg-wait", "win-message-time"):
        _t["hold"] = "written by rt-win-queue with the piece win-queue"


def problems():
    out, seen = [], {}
    for t in WINDOW_TASKS:
        for m in t["members"]:
            seen.setdefault(m, []).append("row " + t["id"])
    for m in BUILT:
        seen.setdefault(m, []).append("BUILT")
    for m in STOPS:
        seen.setdefault(m, []).append("STOPS")
    for m, where in sorted(seen.items()):
        if len(where) > 1:
            out.append("%s is in %s" % (m, " and ".join(where)))
        if m not in IMPORTERS:
            out.append("%s (%s) is not imported by a boot-path DLL from user32, gdi32 or imm32" % (m, where[0]))
    for m in sorted(IMPORTERS):
        if m not in seen:
            out.append("%s!%s is imported and has no row, is not BUILT and is not in STOPS" % (IMPORTERS[m][0], m))
    for t in WINDOW_TASKS:
        for m in t["members"]:
            if m in IMPORTERS and IMPORTERS[m][0] != t["lib"]:
                out.append("row %s: %s is imported from %s, the row says %s" % (t["id"], m, IMPORTERS[m][0], t["lib"]))
    return out


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def unit_exists(uid):
    return os.path.exists(os.path.join(ROOT, "swarm", "units", uid + ".json"))


def table():
    """The contract's table: one line per imported function, by group."""
    rows = {}
    for t in WINDOW_TASKS:
        waits = [w for w in t["waits"] if not unit_exists(w)]
        where = "row `%s`%s" % (t["id"], ", waits for " + ", ".join("`%s`" % w for w in waits) if waits else ", ready")
        for m in t["members"]:
            done = os.path.exists(os.path.join(ROOT, "runtime", "shims", t["lib"], m + ".c"))
            rows[m] = (t["group"], t["says"], "written (row `%s`)" % t["id"] if done else where)
    for m, (g, says) in BUILT.items():
        rows[m] = (g, says, "built")
    for m, (g, why) in STOPS.items():
        rows[m] = (g, "**stop**: " + why, "stop")
    out = []
    for g in GROUPS:
        out += ["", "### " + g[0].upper() + g[1:], "", "| Function | Imported by | With no screen | State |", "|---|---|---|---|"]
        for m in sorted((m for m in rows if rows[m][0] == g), key=str.lower):
            lib, who = IMPORTERS.get(m, ("?", []))
            out.append("| `%s` (%s) | %s | %s | %s |" % (m, lib, ", ".join(who), rows[m][1], rows[m][2]))
    return "\n".join(out)


def summary():
    ready = [t for t in WINDOW_TASKS if not [w for w in t["waits"] if not unit_exists(w)]]
    n = lambda ts: sum(len(t["members"]) for t in ts)
    by = {}
    for t in WINDOW_TASKS:
        for w in t["waits"]:
            if not unit_exists(w):
                by.setdefault(w, []).append(t)
    written = [t for t in ready if all(os.path.exists(os.path.join(ROOT, "runtime", "shims", t["lib"], m + ".c"))
                                       for m in t["members"])]
    lines = ["%d imports: %d answered (%d built, %d in %d rows), %d stops" % (
        len(IMPORTERS), len(BUILT) + n(WINDOW_TASKS), len(BUILT), n(WINDOW_TASKS), len(WINDOW_TASKS), len(STOPS)),
        "ready now: %d rows, %d functions; of them written: %d rows, %d functions" % (
            len(ready), n(ready), len(written), n(written))]
    for w in sorted(by):
        lines.append("waits for %-24s %2d rows, %2d functions" % (w + ":", len(by[w]), n(by[w])))
    return "\n".join(lines)


def importers_block():
    """IMPORTERS as source text, from inventory/inventory.json (import tables only; no game file is opened)."""
    import json
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    sys.path.insert(0, os.path.join(root, "tools", "scout"))
    import boot_import_gap as gap
    inv = json.load(open(os.path.join(root, "inventory", "inventory.json")))
    mods = {f["file"]: f for f in inv["files"] if f["machine"] == "x86-64"
            and (f["rel"].startswith("bin/x64_retail/") or f["rel"] == "server.dll")}
    who = {}
    for _stage, names in gap.STAGES:
        for mod in names:
            if mod in gap.STUBBED:
                continue
            for kind in ("imports", "delay_imports"):
                for lib, fns in mods[mod][kind].items():
                    if lib.lower() in ("user32.dll", "gdi32.dll", "imm32.dll"):
                        for fn in fns:
                            who.setdefault(fn, (lib.lower()[:-4], []))[1].append(mod[:-4])
    lines = ["IMPORTERS = {"]
    for fn in sorted(who, key=str.lower):
        lines.append("    %r: (%r, %r)," % (fn, who[fn][0], who[fn][1]))
    return "\n".join(lines + ["}"])


if __name__ == "__main__":
    if sys.argv[1:] == ["--importers"]:
        print(importers_block())
        sys.exit(0)
    bad = problems()
    if sys.argv[1:] == ["--check"]:
        print("\n".join(bad + [summary()]))
        sys.exit(1 if bad else 0)
    if sys.argv[1:] == ["--summary"]:
        print(summary())
    else:
        print(table())
