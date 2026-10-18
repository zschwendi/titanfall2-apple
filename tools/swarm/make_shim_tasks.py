#!/usr/bin/env python3
"""Write briefs and unit files for small shim tasks: one or two Windows functions per worker.

Second generation of tools/swarm/make_shim_units.py, shaped for cost (the owner, 2026-10-09): a
Haiku request over 100,000 tokens costs five times as much, and the first wave's workers,
given five to nine functions and a long reading list, ended at 160,000 to 300,000.  So a task
here is one function or an A/W pair, the brief carries the excerpt of the core header the
worker needs instead of sending it to read whole files, and the reading list is one page
(runtime/README.md) and one model shim with its test.

    .venv/bin/python tools/swarm/make_shim_tasks.py            write briefs and unit files
    .venv/bin/python tools/swarm/make_shim_tasks.py --show ID  print one brief
    .venv/bin/python tools/swarm/make_shim_tasks.py --check    check the table; non-zero and why
    .venv/bin/python tools/swarm/make_shim_tasks.py --list     one line per row
    .venv/bin/python tools/swarm/make_shim_tasks.py --sizes    characters of each brief, largest first

Units are kind "shim-task", model haiku; run them with tools/swarm/wave.py.
A row's `needs` names the ledger units whose merge makes its core real: a row may be added
before its core is built (the header must declare the functions), and the ledger holds the
unit back until every need is merged.  Every type in a row's prototypes must be in
runtime/tools/types.def and every constant under runtime/include/rt/win/ (planner's job);
`--check` proves the first, and nothing is written while it fails.
Re-running overwrites briefs and leaves existing unit files (and their state) alone.

A row may also name `waits`: units that will build its core piece and have no file under
swarm/units/ yet.  Such a row is checked, listed and shown, and held until they have (the rows
of tools/swarm/shim_rows_window.py use it).

--check fails when: a row's header does not exist or does not declare a core function the row
names; the model shim or its test does not exist; a function is in two rows; a `needs` names a
unit with no file under swarm/units/; a function is not in runtime/tools/imports.def or
imports_extra.def (shim-check would refuse the shim); a function has no prototype (`protos`,
or an existing shim's SHIM line); a prototype names a type that is not in types.def, as
runtime/tools/shimdef.py reads a SHIM line.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "runtime", "tools"))
import shimdef                                                     # noqa: E402

# id, lib, members, needs, core header, core functions to excerpt, model shim, notes; optionally
# more=[(header, [functions])] for a second header the shim calls;
# protos=[...]: the prototype of each member as its SHIM line will have it, in C form
# ("BOOL CloseHandle(HANDLE hObject)"): the brief prints the SHIM line and --check proves the
# types.  A row whose answer is fixed and calls no core has header=None and core=[].
# hold="why": the row is checked, listed and shown, but no brief or unit is written for it until
# the reason is gone (delete the key then).  Two spellings are in use: "orchestrator: ..." (a
# proposal the orchestrator has to release) and "core: ..." (a core piece that no unit builds yet).
# model="lib/Name" names a model shim outside kernel32.  decl_only=[core functions]: the brief
# prints only their declaration, not the header's comment, because the comment is long and the
# notes carry what the worker needs of it.  stage="A".."E": the boot stage that first needs the
# row (tools/scout/boot_import_gap.py); the unit's priority follows it, A first.
# The rows of round 3 (unit rt-shim-rows-2) are in tools/swarm/shim_rows_r3.py, appended below.
A_NAME = """\
- The `A` function takes the name in the ANSI code page and the core takes it wide. Convert in
  the shim into a `WCHAR wide[MAX_PATH]` on the stack with
  `rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH)`; a result of 0 means the name does not
  fit: fail with `ERROR_FILENAME_EXCED_RANGE` (that is what kernel32 does for an object name
  over `MAX_PATH`). A NULL name is passed to the core as NULL (an unnamed object).
"""
SEC = """\
- The security-attributes pointer is ignored, NULL or not: `bInheritHandle` has no effect
  because this runtime never creates a child process (planner's decision, 2026-10-09). Say so in
  a comment; it is not a trap.
"""
SYNC_ERR = """\
- The core functions of `rt/sync.h` take `ctx` and set last error themselves, also the
  `ERROR_SUCCESS` or `ERROR_ALREADY_EXISTS` of a successful create. The shim does not touch it.
"""
LOCALE_ERR = """\
- The core functions of `rt/locale.h` set the calling thread's last error themselves on failure
  and leave it alone on success. The shim never sets it; the test checks every code.
"""

# ---- sockets (unit rt-contract-net): shared notes of the block at the end of TASKS ----
NET_ORD = """\
- `wsock32.dll` exports these functions by number and the game imports them that way, so the
  shim is declared with `SHIM_ORD(wsock32, <ordinal>, ...)` exactly as the line above shows; the
  file is still named after the function. The test calls it by name: `CALL(wsock32, <name>, ...)`.
"""
NET_PURE = """\
- This function has no state: it needs no `WSAStartup`, looks at no socket and never touches
  last error. `RT-DOC` is its page under
  `https://learn.microsoft.com/windows/win32/api/winsock2/` (or `ws2tcpip` for the `inet_`
  functions of `ws2_32`).
"""
NET_ERR = """\
- The functions of `rt/net.h` set last error (which is what `WSAGetLastError` returns) themselves:
  to the code when they fail, and TO 0 WHEN THEY SUCCEED for the ten functions the header's LAST
  ERROR paragraph lists (`socket`, `bind`, `getsockname`, `shutdown`, `setsockopt`, `getsockopt`,
  `sendto`/`send`, `recvfrom`/`recv`, `getaddrinfo`, `WSACleanup`: Windows does, Wine
  `ws2_32/tests/sock.c` and `protocol.c`; review D22 of `docs/reviews/contracts-check-2.md`); the
  others leave it alone on success. The shim returns the result and sets nothing; the test
  checks every code with `T_EQ(T_LAST_ERROR(), WSAE...)` (the codes are in `rt/win/net.h`) and,
  for a function of that list, has one case: last error 0xDEAD, a call that succeeds, last
  error 0.
"""
NET_SETUP = """\
- A test starts Winsock itself, with `WSADATA d; T_EQ(rt_net_startup(0x0202, &d), 0);`, makes
  sockets with `rt_net_socket` and closes them with `rt_net_close` (all `rt/net.h`, called
  directly: a test does not go through another shim to set up). Addresses are the structures
  of `rt/win/net.h` (`SOCKADDR_IN`, `SOCKADDR_IN6`), filled by hand with ports in network byte
  order (port 4000 is 0xA00F as a `USHORT`). No test opens a host socket or sends a packet:
  there is no host network call anywhere in this runtime.
"""
NET_DATA = """\
- The buffer is `LPCVOID` or `char *`, never a string type: it holds bytes, and the call trace
  must not read it as text.
"""
NET_TRAP = """\
- Status `trap`, decided in `docs/handoff/rt-contract-net.md` ("Stops"): on the machine of
  `rt/net.h` nothing on the way to the campaign calls this function, and its callers in the
  game are tools behind a command-line switch or a developer setting. `RT-CASES: none`,
  `RT-TRAPS: every call`. The whole body is the one line below and nothing follows it (the
  macro does not return). `runtime/tests/abi/rttest/TrapOnly.c` is a shim of this status. The
  test is one statement with no `T_CASE`.
"""
NET_STOPS = """\
- A stop of `rt/net.h` ends the run with the text `<core function>: not implemented: <what>`.
  The `T_TRAPS` line that proves one expects that function's name and the two words, for
  example `T_TRAPS(CALL(...), "rt_net_startup: not implemented");`, not just "FATAL".
"""
NET_HOLD = "waits for the core piece rt-net-%s (docs/handoff/rt-contract-net.md, task table): no unit yet"

CORE_STOPS = """\
- Where the core's comment says a case STOPS or is fatal, the shim adds no guard for it. List
  each such case that a test can reach in `RT-TRAPS` as `core: <case>` (the status is then
  `partial`) and prove each listed one with a `T_TRAPS`; name the ones you left out in your reply.
"""
FILE_ERR = """\
- The functions of `rt/file.h` set last error themselves when they fail and leave it alone on
  success. The shim returns the result and sets nothing; the test checks every code.
"""
FILE_TEST = """\
- A test may only touch files under the install root the harness made: the guest's
  `C:\\Titanfall2\\...` is the host's `rt_test_install_root()`. Copy the helpers `W`, `I` and `put`
  from the top of `runtime/tests/core/file_open.c` (read its first 60 lines and nothing else of
  it). Open a handle with `rt_file_create` (`rt/file.h`) and close it with `rt_handle_close`
  (`rt/handle.h`).
"""
FILE_NAME = """\
- The name goes to the core as `RT_NAME_A(name)` or `RT_NAME_W(name)` (`rt/file.h`), NULL
  included: the shim converts nothing and looks at no character of it.
"""
MOD_ERR = """\
- The functions of `rt/module.h` set last error themselves when they fail and leave it alone on
  success. The shim sets nothing. An `HMODULE` is a module's base: `(HMODULE)(uintptr_t)m->base`
  for an `RtModule *m`. A test finds a module with `rt_module_find("kernel32.dll")` (a native
  module, always loaded); do not load a lifted module in a shim test.
"""
THREAD_ERR = """\
- The functions of `rt/thread.h` set last error themselves when they fail and leave it alone on
  success. The shim returns the result and sets nothing.
"""
ABSENT = """\
- Status `absent`: this runtime has no console (`docs/RUNTIME.md` section 7.3; the standard
  handles are character devices that are not consoles), so each call fails as Windows fails
  when the process has no console. The answers are signed off in
  `docs/handoff/rt-contract-file.md` ("Shims this unlocks"): the return values are the
  documented failure returns, the last-error codes are Wine's as remembered and not verified.
  `RT-TRAPS` is `none`; no argument is looked at and nothing is written through any pointer.
- An `absent` shim logs: `rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!<Name>", "<Name>:
  there is no console");` before it fails. The test checks the line with
  `T_LOGGED(RT_CH_FILE, "<Name>: there is no console")`.
"""
A_PATH = """\
- `rt/path.h` has no A form, so the `A` shim converts (decision of the file contract,
  `docs/handoff/rt-contract-file.md`): a name comes in through `WCHAR wide[MAX_PATH]` on the
  stack and `rt_mb_to_wide(CP_ACP, 0, name, -1, wide, MAX_PATH)`, where a result of 0 means the
  name does not fit and is `RT_UNIMPLEMENTED`; a result goes out through a `char[MAX_PATH]` on
  the stack and `rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, n + 1, narrow, MAX_PATH, NULL,
  &used)` (n characters and the terminator), where a result of 0 or `used` TRUE (a character
  outside code page 1252) is `RT_UNIMPLEMENTED`. In code page 1252 one WCHAR is one byte, so
  lengths and indexes are the same in both forms. Last error after `RT_UNIMPLEMENTED` does not
  matter: the run has ended.
"""
PATH_ERR = """\
- The functions of `rt/path.h` never touch last error: a function that returns a Win32 error
  code is used as `DWORD e = rt_path_...; if (e) return FAIL(ctx, e, <failure value>);`.
"""

TASKS = [
    dict(
        id="sleep", lib="kernel32", members=["Sleep", "SleepEx"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_sleep_ex", "rt_apc_queue"], model="GetTickCount",
        notes="""\
- `Sleep` returns nothing and is never alertable. `SleepEx` returns 0 when the time ran out and
  `WAIT_IO_COMPLETION` when an asynchronous procedure call ran (alertable only).
- A time of 0 gives up the rest of the time slice and returns; `INFINITE` does not time out.
- Tests: a 30 ms sleep lasts at least 30 ms on the core's clock (`rt_time_mono_100ns`, in
  `rt/time.h`) and well under 5 s; a time of 0 returns at once; last error is left alone. For the
  alertable case see whether a test can queue a call to its own thread with `rt_apc_queue`; if
  the core cannot do that from a test, say so in your reply instead of forcing it.""",
    ),
    # ---- locale and strings (core: rt-core-locale) ----
    dict(
        id="acp", lib="kernel32", members=["GetACP", "GetOEMCP"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_cp_acp", "rt_cp_oemcp"], model="GetTickCount",
        notes="""\
- Two getters with no arguments and no failure. Tests: the values the core's comment names, and
  that last error is left alone.""",
    ),
    dict(
        id="cpinfo", lib="kernel32", members=["IsValidCodePage", "GetCPInfo"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_cp_is_valid", "rt_cp_info"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- `GetCPInfo(UINT CodePage, LPCPINFO lpCPInfo)`: `CPINFO` is in `rt/win/nls.h` (20 bytes). Test
  every byte the function writes for 1252, 437 and `CP_UTF8`, and that a failing call writes
  nothing.
- What a NULL `lpCPInfo` does is the core's to say; if its comment does not say, trap.""",
    ),
    dict(
        id="mb2wc", lib="kernel32", members=["MultiByteToWideChar"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_mb_to_wide"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int
  cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)`. `LPCCH` is counted text (class ptr).
- The shim is one call. The core's comment is the specification, including which argument
  errors it reports and which it stops on; do not add checks of your own.
- Tests, through the guest ABI: a length of -1 (the terminator is converted and counted), an
  explicit length, the size query (`cchWideChar` 0), a buffer one too small
  (`ERROR_INSUFFICIENT_BUFFER`), `MB_ERR_INVALID_CHARS` on invalid UTF-8
  (`ERROR_NO_UNICODE_TRANSLATION`), a bad flag (`ERROR_INVALID_FLAGS`), code pages 1252, 437,
  `CP_ACP` and `CP_UTF8`.""",
    ),
    dict(
        id="wc2mb", lib="kernel32", members=["WideCharToMultiByte"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_wide_to_mb"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int
  cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL
  lpUsedDefaultChar)`. Eight arguments: four go on the stack, which the thunk handles.
- The shim is one call. The core's comment is the specification; do not add checks of your own.
- Tests: -1 and explicit lengths, the size query, a buffer one too small, the default character
  and the used-default flag (all four combinations of the two pointers) with
  `WC_NO_BEST_FIT_CHARS` on 1252, `CP_UTF8` with the two pointers non-NULL
  (`ERROR_INVALID_PARAMETER`), a bad flag.""",
    ),
    dict(
        id="strtype", lib="kernel32", members=["GetStringTypeW"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_string_type"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `BOOL GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD
  lpCharType)`. One core call.
- Tests: `CT_CTYPE1` for a letter of each case, a digit, a space and a control character;
  `CT_CTYPE2` and `CT_CTYPE3` for one character each; a length of -1; the failures the core's
  comment names.""",
    ),
    dict(
        id="lcmap", lib="kernel32", members=["LCMapStringW"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_lcmap_string"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `int LCMapStringW(LCID Locale, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc,
  LPWSTR lpDestStr, int cchDest)`. One core call.
- Tests: upper and lower case with locale `0x409`, `LOCALE_USER_DEFAULT` and 0 (the C runtime's
  start-up passes 0); the size query; a buffer one too small; -1 and explicit lengths; a flag
  combination the core rejects.""",
    ),
    dict(
        id="cmpstr", lib="kernel32", members=["CompareStringW"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_compare_string"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `int CompareStringW(LCID Locale, DWORD dwCmpFlags, PCNZWCH lpString1, int
  cchCount1, PCNZWCH lpString2, int cchCount2)`; write the two string types as `LPCWCH`.
- Tests: the three results (`CSTR_LESS_THAN`, `CSTR_EQUAL`, `CSTR_GREATER_THAN`), with and
  without `NORM_IGNORECASE`, -1 and explicit lengths, a prefix against a longer string, a NULL
  string (`ERROR_INVALID_PARAMETER`), a bad flag.""",
    ),
    dict(
        id="deflcid", lib="kernel32", members=["GetUserDefaultLCID", "GetUserDefaultUILanguage"],
        needs=["rt-core-locale"], header="rt/locale.h", core=["rt_locale_default"], model="GetTickCount",
        notes="""\
- `LCID GetUserDefaultLCID(void)` is the core's value. `LANGID GetUserDefaultUILanguage(void)`
  is its low 16 bits (the language identifier of the same locale). No failure; last error is
  left alone.""",
    ),
    dict(
        id="validlocale", lib="kernel32", members=["IsValidLocale"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_locale_is_valid"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- One core call. Tests: `0x409` with `LCID_INSTALLED` and with `LCID_SUPPORTED`; the failure
  the core's comment names; `T_TRAPS` for what the core stops on (another locale).""",
    ),
    dict(
        id="localeinfo", lib="kernel32", members=["GetLocaleInfoW"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_locale_info"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `int GetLocaleInfoW(LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData)`.
  One core call.
- Tests: `LOCALE_SNAME`, `LOCALE_SDECIMAL`, `LOCALE_IDEFAULTANSICODEPAGE` as text and with
  `LOCALE_RETURN_NUMBER`; the size query; a buffer one too small; the failures the core names.""",
    ),
    dict(
        id="enumlocales", lib="kernel32", members=["EnumSystemLocalesW"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_locale_enum_begin", "rt_locale_enum_next"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototype: `BOOL EnumSystemLocalesW(LOCALE_ENUMPROCW lpLocaleEnumProc, DWORD dwFlags)`. The
  comment above `rt_locale_enum_begin` gives the loop; the callback is guest code, called with
  `GUEST_CALL(ctx, proc, name)`.
- A NULL callback: the documentation's failure code if it names one, else trap.
- The test needs a guest function for the callback: `runtime/include/rt/test.h` describes
  `T_GUEST_FN` and `T_GUEST_ADDR`. Test that the callback is called once with `"00000409"`, that
  a callback returning FALSE stops the enumeration and the function still returns TRUE, and the
  flags the core rejects.""",
    ),
    dict(
        id="datefmt", lib="kernel32", members=["GetDateFormatW", "GetTimeFormatW"], needs=["rt-core-locale"],
        header="rt/locale.h", core=["rt_date_format", "rt_time_format"], model="GetTickCount",
        notes=LOCALE_ERR + """\
- Prototypes: `int GetDateFormatW(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpDate, LPCWSTR
  lpFormat, LPWSTR lpDateStr, int cchDate)` and the same shape for `GetTimeFormatW`.
  `SYSTEMTIME` is in `rt/win/base.h`; `RtSystemTime` in the core's prototypes is the same type.
- One core call each. Tests: a fixed date and time with flags 0 and with a format picture, the
  size query, a buffer one too small, a NULL date (the current one: check only that the call
  succeeds), an invalid date, a bad flag.""",
    ),
    # ---- environment (core: rt-core-env) ----
    dict(
        id="envstrings", lib="kernel32", members=["GetEnvironmentStringsW", "FreeEnvironmentStringsW"],
        needs=["rt-core-env"], header="rt/env.h", core=["rt_env_block_w", "rt_env_block_free"],
        model="GetTickCount",
        notes="""\
- `rt_env_block_w` reports its failure through `*err`: the shim sets last error to it and
  returns NULL (`return FAIL(ctx, err, NULL);`). Success leaves last error alone.
- `FreeEnvironmentStringsW(LPWCH penv)`: the core's comment says what it returns for a pointer
  that is not a block; give that the documentation's failure code if it names one, else
  `ERROR_INVALID_PARAMETER` is NOT to be guessed: trap.
- Tests: the block ends in two NULs, holds `SystemRoot=C:\\Windows`, is a snapshot (a later
  change does not show in it), and is freed once.""",
    ),
    dict(
        id="setenv", lib="kernel32", members=["SetEnvironmentVariableA", "SetEnvironmentVariableW"],
        needs=["rt-core-env"], header="rt/env.h", core=["rt_env_set_a", "rt_env_set_w", "rt_env_get_w"],
        model="GetTickCount",
        notes="""\
- The core reports failure through `*err`: on FALSE the shim sets last error to it
  (`return FAIL(ctx, err, FALSE);`). Success leaves last error alone.
- Tests read the variable back with `rt_env_get_w` (the core; a test may call the core). Set,
  replace, delete with a NULL value, delete what is not there (`ERROR_ENVVAR_NOT_FOUND`), the
  empty name, and a name beginning with `=` such as `"=C:"` (allowed: the C runtime sets it).""",
    ),
    dict(
        id="cmdline", lib="kernel32", members=["GetCommandLineA"], needs=["rt-core-env"],
        header="rt/env.h", core=["rt_cmdline_a"], model="GetTickCount",
        notes="""\
- `LPSTR GetCommandLineA(void)`: no failure. Tests: the pointer is the same on every call,
  the line is not empty and is what `rt_cmdline_a()` gives, last error is left alone.""",
    ),
    dict(
        id="startupinfo", lib="kernel32", members=["GetStartupInfoW"], needs=["rt-core-env"],
        header="rt/env.h", core=["rt_env_startup_info"], model="GetTickCount",
        notes="""\
- `void GetStartupInfoW(LPSTARTUPINFOW lpStartupInfo)`; the structure is in
  `rt/win/process.h` (104 bytes). Test every field the core's comment names, that all 104 bytes
  are written and nothing after them, and that last error is left alone. What a NULL pointer
  does is in the core's comment.""",
    ),
    # ---- events, mutexes, semaphores, waits (core: rt-core-wait) ----
    dict(
        id="event-create", lib="kernel32", members=["CreateEventA"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_event_create", "rt_wait_handle"], model="CloseHandle",
        more=[("rt/locale.h", ["rt_mb_to_wide"])],
        notes=SYNC_ERR + SEC + A_NAME + """\
- Tests (close every handle with `rt_handle_close`, `rt/handle.h`): the initial state, seen
  with `rt_wait_handle(T_CTX(), h, 0, FALSE)`; manual-reset stays set after a wait and
  auto-reset does not; a second create with the same name gives a handle to the same event and
  `ERROR_ALREADY_EXISTS`, a first one `ERROR_SUCCESS`; a name of `MAX_PATH` characters or more.""",
    ),
    dict(
        id="event-set", lib="kernel32", members=["SetEvent", "ResetEvent"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_event_set", "rt_event_reset", "rt_event_create", "rt_wait_handle"],
        model="CloseHandle",
        notes=SYNC_ERR + """\
- One core call each. The test makes its events with `rt_event_create(T_CTX(), ...)` and reads
  the state with `rt_wait_handle(T_CTX(), h, 0, FALSE)`. Cases: set then reset, setting twice,
  an auto-reset event consumed by one wait, a handle that is not open and a handle of another
  kind (`ERROR_INVALID_HANDLE`), success leaves last error alone.""",
    ),
    dict(
        id="event-open", lib="kernel32", members=["OpenEventA"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_event_open", "rt_event_create"], model="CloseHandle",
        more=[("rt/locale.h", ["rt_mb_to_wide"])],
        notes=SYNC_ERR + A_NAME + """\
- `HANDLE OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)`. The access
  mask and the inherit flag are ignored (no access checks and no child processes in this
  runtime); say so in a comment.
- Tests: open an event the test created with `rt_event_create` and see that setting one handle
  shows through the other; a name that does not exist (`ERROR_FILE_NOT_FOUND`); a NULL name
  (the code the core's comment gives).""",
    ),
    dict(
        id="mutex", lib="kernel32", members=["CreateMutexA", "ReleaseMutex"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_mutex_create", "rt_mutex_release", "rt_wait_handle"], model="CloseHandle",
        more=[("rt/locale.h", ["rt_mb_to_wide"])],
        notes=SYNC_ERR + SEC + A_NAME + """\
- Tests, on one thread: created owned, it is released once and a second release fails with
  `ERROR_NOT_OWNER`; created unowned, a wait takes it and waits nest (two waits, two releases);
  a named mutex created twice (`ERROR_ALREADY_EXISTS`, and the second caller does not own it);
  a handle of another kind (`ERROR_INVALID_HANDLE`).""",
    ),
    dict(
        id="sem-create", lib="kernel32", members=["CreateSemaphoreA", "CreateSemaphoreW"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_semaphore_create", "rt_wait_handle"], model="CloseHandle",
        more=[("rt/locale.h", ["rt_mb_to_wide"])],
        notes=SYNC_ERR + SEC + A_NAME + """\
- Tests: an initial count of 2 satisfies two waits with timeout 0 and the third times out; the
  three invalid count combinations (`ERROR_INVALID_PARAMETER`); a named semaphore created twice.""",
    ),
    dict(
        id="sem-release", lib="kernel32", members=["ReleaseSemaphore"], needs=["rt-core-wait"],
        header="rt/sync.h", core=["rt_semaphore_release", "rt_semaphore_create", "rt_wait_handle"],
        model="CloseHandle",
        notes=SYNC_ERR + """\
- `BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount)`: one
  core call. Tests: the previous count is stored on success and not written on failure; a NULL
  `lpPreviousCount`; a release count of 0 and a negative one; passing the maximum
  (`ERROR_TOO_MANY_POSTS`, count unchanged); a handle of another kind.""",
    ),
    dict(
        id="wait-single", lib="kernel32", members=["WaitForSingleObject", "WaitForSingleObjectEx"],
        needs=["rt-core-wait"], header="rt/sync.h",
        core=["rt_wait_handle", "rt_event_create", "rt_mutex_create", "rt_semaphore_create"], model="Sleep",
        notes=SYNC_ERR + """\
- `WaitForSingleObject` is never alertable; `WaitForSingleObjectEx` passes `bAlertable` on.
- Tests, on one thread: a set event (`WAIT_OBJECT_0`), a reset event with timeout 0 and with
  20 ms (`WAIT_TIMEOUT`, and at least 20 ms passed on `rt_time_mono_100ns`, `rt/time.h`), a
  semaphore count going down, a mutex taken twice, a handle that is not open (`WAIT_FAILED`
  and `ERROR_INVALID_HANDLE`), success leaves last error alone.""",
    ),
    dict(
        id="wait-multi", lib="kernel32", members=["WaitForMultipleObjects"],
        needs=["rt-core-wait"], header="rt/sync.h",
        core=["rt_wait_handles", "rt_event_create", "rt_event_set"], model="Sleep",
        notes=SYNC_ERR + """\
- `DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD
  dwMilliseconds)`: one core call, never alertable.
- Tests, on one thread: wait-any returns the index of the lowest set event; wait-all times out
  until every event is set; a count of 0 and of `MAXIMUM_WAIT_OBJECTS + 1`
  (`ERROR_INVALID_PARAMETER`); the same handle twice with wait-all; a handle that is not open.""",
    ),
    # ---- thread-local storage and threads (core: rt-contract-thread, TLS body from rt-core-tasks) ----
    dict(
        id="tls-alloc", lib="kernel32", members=["TlsAlloc", "TlsFree"], needs=["rt-core-tasks"],
        header="rt/thread.h", core=["rt_tls_alloc", "rt_tls_free"], model="GetTickCount",
        protos=["DWORD TlsAlloc(void)",
                "BOOL TlsFree(DWORD dwTlsIndex)"],
        notes="""\
- One core call each. The comment of each core function says what it returns, which last error
  it sets and who sets it: follow it to the letter. `TLS_OUT_OF_INDEXES` is in
  `rt/win/process.h`.
- Tests: an index that was allocated can be freed once; a freed index is given out again;
  freeing an index that is not allocated, and one that is out of range, give the documented
  failure; success leaves last error alone. Free every index the test allocates.""",
    ),
    dict(
        id="tls-value", lib="kernel32", members=["TlsGetValue", "TlsSetValue"], needs=["rt-core-tasks"],
        header="rt/thread.h", core=["rt_tls_get", "rt_tls_set", "rt_tls_alloc", "rt_tls_free"], model="GetTickCount",
        protos=["LPVOID TlsGetValue(DWORD dwTlsIndex)",
                "BOOL TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue)"],
        notes="""\
- `LPVOID TlsGetValue(DWORD dwTlsIndex)` and `BOOL TlsSetValue(DWORD dwTlsIndex, LPVOID
  lpTlsValue)`: one core call each; the value is a pointer on the guest side and a `uint64_t`
  in the core.
- **Last error is special here**: read what the comment of `rt_tls_get` says about last error on
  success and do exactly that (a successful `TlsGetValue` is one of the few calls that write
  it). The test checks it both ways: a stored 0 read back with last error cleared, and a
  failure with its code.
- Tests allocate their index with `rt_tls_alloc()` and free it with `rt_tls_free`. Cases: a
  fresh slot reads 0; a value written is read back, all 64 bits; two slots do not disturb each
  other; an index that is out of range, for both functions; an index that is not allocated,
  as the core's comments say.""",
    ),
    dict(
        id="thread-create", lib="kernel32", members=["CreateThread", "ResumeThread"], needs=["rt-contract-thread"],
        header="rt/thread.h", core=["rt_thread_create", "rt_thread_resume"], model="CloseHandle",
        protos=["HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)",
                "DWORD ResumeThread(HANDLE hThread)"],
        more=[("rt/sync.h", ["rt_wait_handle"])],
        notes="""\
- `HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
  LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD
  lpThreadId)`: the security attributes are ignored (no child processes: say so in a comment),
  the rest is one core call. `DWORD ResumeThread(HANDLE hThread)`: one core call.
- This test is the exception to "do not start threads in a shim test": it must. Copy how
  `runtime/tests/core/thread_run.c` does it: the start routine is a guest function
  (`T_GUEST_FN`, see `rt/test.h`) that stores its argument in a `static` and returns a code;
  the main thread waits with `rt_wait_handle(T_CTX(), h, 5000, FALSE)` and only then looks.
  Use `T_` macros on the main thread only. Close every handle with `rt_handle_close`.
- Cases: a thread runs its start routine with the parameter and the handle becomes signalled;
  `lpThreadId` receives a non-zero id and may be NULL; `CREATE_SUSPENDED` (in
  `rt/win/process.h`) does not run until `ResumeThread`, which returns the previous suspend
  count 1; `ResumeThread` on a handle that is not a thread fails as the core's comment says;
  the creation flags the core stops on are in its comment: prove one with `T_TRAPS`.""",
    ),
    dict(
        id="thread-exit", lib="kernel32", members=["ExitThread"], needs=["rt-contract-thread"],
        header="rt/thread.h", core=["rt_thread_exit", "rt_thread_create"], model="Sleep",
        protos=["void ExitThread(DWORD dwExitCode)"],
        more=[("rt/sync.h", ["rt_wait_handle"])],
        notes="""\
- `void ExitThread(DWORD dwExitCode)`: one core call that does not return.
- The test must start a thread (the exception to the rule below): copy
  `runtime/tests/core/thread_run.c`. The start routine is a guest function (`T_GUEST_FN`) that
  calls the shim through the guest ABI with `CALL(kernel32, ExitThread, 7)` and then, on a
  line that must never run, sets a `static` flag. The main thread creates it with
  `rt_thread_create`, waits with `rt_wait_handle(T_CTX(), h, 5000, FALSE)`, and checks that
  the flag is clear. `T_` macros on the main thread only.
- Do **not** call `ExitThread` on the test's own thread: on the last thread it ends the
  process (the core's comment says so), which another unit builds.""",
    ),
    # ---- files (core: rt-contract-file; open is built) ----
    dict(
        id="file-create", lib="kernel32", members=["CreateFileW", "CreateFileA"], needs=["rt-contract-file"],
        header="rt/file.h", core=["rt_file_create"], model="CloseHandle",
        protos=["HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)",
                "HANDLE CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)"],
        notes="""\
- Seven arguments each: `(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
  dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)`. The security attributes are
  ignored (no child processes: say so in a comment). The name goes to the core as
  `RT_NAME_W(lpFileName)` or `RT_NAME_A(lpFileName)` (`rt/file.h`): the shim converts nothing
  and touches no path.
- **Last error**: the core's comment says it writes last error on success as well as on
  failure. The shim adds nothing; the test checks both.
- The test may only create files under the directories the harness gives it: read in
  `runtime/tests/core/file_open.c` how a test names a file there (the guest path and the two
  helper functions) and copy that, nothing else from it. Close every handle with
  `rt_handle_close` (`rt/handle.h`).
- Cases: `CREATE_NEW` makes a file and fails the second time with `ERROR_FILE_EXISTS`;
  `OPEN_EXISTING` on a missing file fails with `ERROR_FILE_NOT_FOUND` and returns
  `INVALID_HANDLE_VALUE`; `CREATE_ALWAYS` and `OPEN_ALWAYS` report an existing file through
  last error as the core's comment says; A and W open the same file; a NULL name and an
  invalid disposition, as the core's comment says.""",
    ),
    # ==== Gate 2 (unit rt-shim-rows, 2026-10-09): what tier0.dll and vstdlib.dll still lack ====
    # Order: (a) the C runtime's start-up and exit, (b) a platform layer's initialisation,
    # (c) files, the loader's remaining forms and the rest.  docs/handoff/rt-shim-rows.md has
    # the decisions that are from memory and the functions that have no row.
    # ---- (a) standard handles, file type, reading and writing ----
    dict(
        id="std-handle", lib="kernel32", members=["GetStdHandle", "SetStdHandle"], needs=["core-file-std-body"],
        header="rt/file.h", core=["rt_std_get", "rt_std_set"], model="CloseHandle",
        protos=["HANDLE GetStdHandle(DWORD nStdHandle)", "BOOL SetStdHandle(DWORD nStdHandle, HANDLE hHandle)"],
        notes=FILE_ERR + """\
- One core call each; the core decides every case. `STD_INPUT_HANDLE`, `STD_OUTPUT_HANDLE` and
  `STD_ERROR_HANDLE` are in `rt/win/file.h`.
- Tests: the three handles are three different values, none NULL or `INVALID_HANDLE_VALUE`, and
  the same on every call; another number (0, 1) gives `INVALID_HANDLE_VALUE` and
  `ERROR_INVALID_HANDLE`; `SetStdHandle` stores any value, NULL included, and `GetStdHandle`
  then returns it (put the original value back before the test ends); `SetStdHandle` with
  another number fails with `ERROR_INVALID_HANDLE`; success leaves last error alone.""",
    ),
    dict(
        id="file-type", lib="kernel32", members=["GetFileType"], needs=["core-file-std-body"],
        header="rt/file.h", core=["rt_file_type", "rt_std_get"], model="CloseHandle",
        protos=["DWORD GetFileType(HANDLE hFile)"],
        notes=FILE_ERR + FILE_TEST + """\
- One core call. Tests: each standard handle (`rt_std_get`) is `FILE_TYPE_CHAR` and a file
  under the install root is `FILE_TYPE_DISK`, both with last error left alone; NULL,
  `INVALID_HANDLE_VALUE` and an event handle (`rt_event_create(T_CTX(), FALSE, FALSE, NULL)`,
  `rt/sync.h`) are `FILE_TYPE_UNKNOWN` with `ERROR_INVALID_HANDLE`.""",
    ),
    dict(
        id="file-rw", lib="kernel32", members=["ReadFile", "WriteFile"], needs=["core-file-io-body"],
        header="rt/file.h", core=["rt_file_read", "rt_file_write", "rt_file_seek"], model="CloseHandle",
        protos=["BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, "
                "LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)",
                "BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, "
                "LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)"],
        notes=FILE_ERR + FILE_TEST + CORE_STOPS + """\
- One core call each, with `lpOverlapped` passed on as it came: the core decides (its step 4).
  **Do not test a call with `lpOverlapped` not NULL**: that path is another unit's and may not
  be built yet; do not list it in `RT-TRAPS` either.
- Do not use the standard handles in these tests (another unit's).
- Tests: bytes written through one handle are read back (move the position with `rt_file_seek`);
  a short read at the end of the file, and end of file itself, which is TRUE with 0 bytes; a
  length of 0 for both; the count is set to 0 before a failure (a handle that is not open:
  `ERROR_INVALID_HANDLE`); reading a handle opened for writing only and writing one opened for
  reading only (`ERROR_ACCESS_DENIED`); a write beyond the end extends the file; the stop
  `core: both lpNumberOfBytes and lpOverlapped NULL`, for each function.""",
    ),
    dict(
        id="file-seek", lib="kernel32", members=["SetFilePointerEx", "SetEndOfFile"], needs=["core-file-io-body"],
        header="rt/file.h", core=["rt_file_seek", "rt_file_set_eof", "rt_file_size"], model="CloseHandle",
        protos=["BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, "
                "PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)",
                "BOOL SetEndOfFile(HANDLE hFile)"],
        notes=FILE_ERR + FILE_TEST + CORE_STOPS + """\
- `LARGE_INTEGER` arrives by value; its 64-bit value is the member `QuadPart`. The core call is
  `rt_file_seek(hFile, liDistanceToMove.QuadPart, lpNewFilePointer ? &lpNewFilePointer->QuadPart :
  NULL, dwMoveMethod)`. `SetEndOfFile` is one call.
- Tests for the seek: `FILE_BEGIN`, `FILE_CURRENT` and `FILE_END` with the new position stored;
  a NULL `lpNewFilePointer`; a position before the start (`ERROR_NEGATIVE_SEEK`, the position
  and `*lpNewFilePointer` unchanged); a method of 3 (`ERROR_INVALID_PARAMETER`); a handle that is
  not open (`ERROR_INVALID_HANDLE`); a position beyond the end is allowed and leaves the size
  (`rt_file_size`) as it was.
- Tests for the end of file: the file is cut at the position and is extended to a position
  beyond the end (check with `rt_file_size`); the position does not move; a handle opened for
  reading only (`ERROR_ACCESS_DENIED`); a handle that is not open.""",
    ),
    dict(
        id="file-flush", lib="kernel32", members=["FlushFileBuffers"], needs=["core-file-io-body"],
        header="rt/file.h", core=["rt_file_flush"], model="CloseHandle",
        protos=["BOOL FlushFileBuffers(HANDLE hFile)"],
        notes=FILE_ERR + FILE_TEST + CORE_STOPS + """\
- One core call. Tests: TRUE for a file opened for writing, last error left alone; a handle
  opened for reading only (`ERROR_ACCESS_DENIED`); NULL and a handle that is not open
  (`ERROR_INVALID_HANDLE`); the stop `core: a character device`, with a handle to the device
  `NUL` (open the name `NUL` with `rt_file_create`, `GENERIC_WRITE`, `OPEN_EXISTING`).""",
    ),
    # ---- (a) modules: what the C runtime asks the loader ----
    dict(
        id="module-filename", lib="kernel32", members=["GetModuleFileNameW", "GetModuleFileNameA"],
        needs=["core-loader-query-body"], header="rt/module.h",
        core=["rt_module_from_handle", "rt_module_filename_w", "rt_module_filename_a"], model="CloseHandle",
        protos=["DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)",
                "DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)"],
        notes=MOD_ERR + CORE_STOPS + """\
- Two calls: `RtModule *m = rt_module_from_handle(hModule); if (!m) return 0;` (the core has
  set last error), then `return rt_module_filename_w(m, lpFilename, nSize);`.
- Tests: a NULL module is the executable (compare with what the core gives for
  `rt_module_from_handle(NULL)`); kernel32's name is `C:\\Windows\\system32\\kernel32.dll` and the
  return is its length; a buffer one character too small returns `nSize`, is terminated and
  sets `ERROR_INSUFFICIENT_BUFFER`; `nSize` 0; a handle that is no module, such as
  `(HMODULE)0x1234` (`ERROR_MOD_NOT_FOUND`); success leaves last error alone; the stop
  `core: lpFilename NULL with nSize not 0`.""",
    ),
    dict(
        id="module-handle", lib="kernel32", members=["GetModuleHandleW", "GetModuleHandleA"],
        needs=["core-loader-query-body"], header="rt/module.h",
        core=["rt_module_handle_w", "rt_module_handle_a"], model="CloseHandle",
        protos=["HMODULE GetModuleHandleW(LPCWSTR lpModuleName)", "HMODULE GetModuleHandleA(LPCSTR lpModuleName)"],
        notes=MOD_ERR + """\
- `RtModule *m = rt_module_handle_w(lpModuleName); return m ? (HMODULE)(uintptr_t)m->base : NULL;`
- Tests: `kernel32.dll`, `KERNEL32`, `kernel32` and `C:\\Windows\\system32\\kernel32.dll` all give
  kernel32's base; a NULL name gives what the core gives for NULL (the executable; use the core
  as the oracle, whatever it answers in the test process); a name no module has gives NULL and
  `ERROR_MOD_NOT_FOUND`; success leaves last error alone.""",
    ),
    dict(
        id="module-handle-ex", lib="kernel32", members=["GetModuleHandleExW", "GetModuleHandleExA"],
        needs=["core-loader-query-body"], header="rt/module.h", core=["rt_module_handle_ex"], model="CloseHandle",
        protos=["BOOL GetModuleHandleExW(DWORD dwFlags, LPCVOID lpModuleName, HMODULE *phModule)",
                "BOOL GetModuleHandleExA(DWORD dwFlags, LPCVOID lpModuleName, HMODULE *phModule)"],
        notes=MOD_ERR + """\
- `lpModuleName` is `LPCVOID` here, not the documentation's `LPCWSTR` / `LPCSTR`: with
  `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` it is an address inside a module, and a string type
  would make the call trace read code as text (planner's decision).
- The shim: `RtModule *m = NULL; BOOL ok = rt_module_handle_ex(dwFlags, lpModuleName, true,
  phModule ? &m : NULL); if (phModule) *phModule = m ? (HMODULE)(uintptr_t)m->base : NULL;
  return ok;` (`false` for the A form). The flags are in `rt/win/pe.h`.
- Tests: by name with `GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT`, and once with flags 0
  (`m->load_count` goes up by one: put it back with `m->load_count--` under no lock, the test
  is single-threaded); by address with kernel32's own base as the address; a NULL address with
  the address flag, and a name no module has (FALSE, `*phModule` NULL, `ERROR_MOD_NOT_FOUND`);
  PIN together with UNCHANGED_REFCOUNT, a flag bit of 8, and `phModule` NULL
  (`ERROR_INVALID_PARAMETER`); success leaves last error alone.""",
    ),
    dict(
        id="getprocaddress", lib="kernel32", members=["GetProcAddress"],
        needs=["core-loader-export-body", "core-loader-query-body"], header="rt/module.h",
        core=["rt_module_from_handle", "rt_module_export"], model="CloseHandle",
        protos=["FARPROC GetProcAddress(HMODULE hModule, LPCVOID lpProcName)"],
        notes=MOD_ERR + """\
- `lpProcName` is `LPCVOID` here, not the documentation's `LPCSTR`: it is a name or an ordinal
  ("it must be in the low-order word; the high-order word must be zero"), and a string type
  would make the call trace read an ordinal as text (the loader contract left the choice to the
  planner; decided here).
- The shim: `RtModule *m = rt_module_from_handle(hModule); if (!m) return 0;` then
  `uintptr_t v = (uintptr_t)lpProcName; if (v < 0x10000) return rt_module_export(m, NULL,
  (uint32_t)v); return rt_module_export(m, (const char *)lpProcName, 0);`. A NULL name is
  ordinal 0, which the core answers. `FARPROC` is a guest code address (`GUESTFN`): return the
  core's value as it is.
- Tests, with the core as the oracle: a function kernel32 has a shim for (`GetTickCount`) gives
  the same non-zero address as `rt_module_export(m, "GetTickCount", 0)`, and last error is
  left alone; a name kernel32 does not have, and `FlsAlloc` (one of the names the core answers
  as absent), give 0 and `ERROR_PROC_NOT_FOUND`; ordinal 1 gives what the core gives for
  ordinal 1; a module handle that is no module gives 0 and `ERROR_MOD_NOT_FOUND`.""",
    ),
    dict(
        id="free-library", lib="kernel32", members=["FreeLibrary"], needs=["rt-loader-load"],
        header="rt/module.h", core=["rt_module_from_handle", "rt_module_free"], model="CloseHandle",
        protos=["BOOL FreeLibrary(HMODULE hLibModule)"],
        notes=MOD_ERR + """\
- `hLibModule` NULL: `RT_UNIMPLEMENTED(ctx, "FreeLibrary(NULL)")`, before the core is asked
  (`rt_module_from_handle(NULL)` means the executable, and what Windows does with a NULL handle
  here is not verified). Otherwise `RtModule *m = rt_module_from_handle(hLibModule); if (!m)
  return FALSE;` and `return rt_module_free(m);`.
- Tests on kernel32: raise `m->load_count` by one, call, and see it back where it was and TRUE
  returned with last error left alone; a handle that is no module (`ERROR_MOD_NOT_FOUND`); the
  NULL stop with `T_TRAPS`. Do not free a module more often than the test raised its count.""",
    ),
    dict(
        id="load-library-w", lib="kernel32", members=["LoadLibraryExW", "LoadLibraryW"], needs=["rt-loader-load"],
        header="rt/module.h", core=["rt_module_load"], model="CloseHandle",
        protos=["HMODULE LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)",
                "HMODULE LoadLibraryW(LPCWSTR lpLibFileName)"],
        notes=MOD_ERR + CORE_STOPS + """\
- `LoadLibraryExW`: `hFile` not NULL is `return FAIL(ctx, ERROR_INVALID_PARAMETER, NULL);` (the
  documentation: "reserved for future use. It must be NULL"; the code is the loader contract's
  choice). Then `RtModule *m = rt_module_load(ctx, lpLibFileName, dwFlags); return m ?
  (HMODULE)(uintptr_t)m->base : NULL;`. `LoadLibraryW` is the same with flags 0.
- Tests, on native modules only: `kernel32.dll` gives kernel32's base and its `load_count` one
  higher (put it back with `m->load_count--`), with last error left alone; a name no module has
  (`ERROR_MOD_NOT_FOUND`); a NULL name and a flag bit of `0x80000000`
  (`ERROR_INVALID_PARAMETER`); `LOAD_WITH_ALTERED_SEARCH_PATH` (8) is accepted; `hFile` not
  NULL; the stop `core: LOAD_LIBRARY_AS_DATAFILE`, called with a real name (`L"kernel32.dll"`):
  with a NULL or empty name the core fails with `ERROR_INVALID_PARAMETER` before it looks at
  that flag.""",
    ),
    dict(
        id="load-library-a", lib="kernel32", members=["LoadLibraryExA", "LoadLibraryA"], needs=["rt-loader-load"],
        header="rt/module.h", core=["rt_module_load_a", "rt_module_load"], model="CloseHandle",
        protos=["HMODULE LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)",
                "HMODULE LoadLibraryA(LPCSTR lpLibFileName)"],
        notes=MOD_ERR + CORE_STOPS + """\
- As the W forms with `rt_module_load_a`, which takes the name's bytes as they came: the shim
  converts nothing. `hFile` not NULL is `return FAIL(ctx, ERROR_INVALID_PARAMETER, NULL);`.
- Tests, on native modules only: `kernel32.dll` and `KERNEL32` give kernel32's base and its
  `load_count` one higher each time (put it back with `m->load_count--`); a name no module has,
  and a name whose last path component has a byte of `0xE9` in it, `"caf\\xE9.dll"`
  (`ERROR_MOD_NOT_FOUND`; in a directory part the byte is ignored and the load succeeds); a
  NULL name (`ERROR_INVALID_PARAMETER`); `hFile` not NULL; the stop `core:
  DONT_RESOLVE_DLL_REFERENCES`, called with a real name (`"kernel32.dll"`): with a NULL or empty
  name the core fails with `ERROR_INVALID_PARAMETER` before it looks at that flag.""",
    ),
    # ---- unwinding (core: rt-unwind; docs/handoff/rt-unwind.md section 5) ----
    dict(
        id="unwind-capture", lib="kernel32", members=["RtlCaptureContext"], needs=["rt-unwind"],
        header="rt/unwind.h", core=["rt_unwind_capture"], model="CloseHandle",
        protos=["void RtlCaptureContext(PCONTEXT ContextRecord)"],
        notes="""\
- Status `partial`, `RT-TRAPS: NULL ContextRecord`. The shim: `ContextRecord` NULL is
  `RT_UNIMPLEMENTED(ctx, "RtlCaptureContext(NULL)")` (Windows faults); otherwise
  `rt_unwind_capture(ctx, ContextRecord);` and nothing else. Last error is never touched.
- The core reads the caller's return address from the guest stack at `ctx->r[4]`, which is
  right inside a shim body: do not move the call into a helper.
- Tests (include `rt/win/exception.h` for `CONTEXT`): fill a `CONTEXT` with `0xAA` bytes,
  set the non-volatile guest registers of the test's context to marks before the call
  (`T_CTX()->r[3] = 0x1111;` is rbx; 5 rbp, 6 rsi, 7 rdi, 12 to 15 the same way; put the old
  values back afterwards), call, and check `Rbx`, `Rbp`, `Rsi`, `Rdi`, `R12` to `R15` hold the
  marks, `Rsp` and `Rip` are not zero and not `0xAAAAAAAAAAAAAAAA`, and `ContextFlags` is
  what the core's comment says; last error left alone; the NULL stop with `T_TRAPS`.""",
    ),
    dict(
        id="unwind-lookup", lib="kernel32", members=["RtlLookupFunctionEntry"], needs=["rt-unwind"],
        header="rt/unwind.h", core=["rt_unwind_lookup"], model="CloseHandle",
        protos=["PRUNTIME_FUNCTION RtlLookupFunctionEntry(DWORD64 ControlPc, PDWORD64 ImageBase, "
                "PUNWIND_HISTORY_TABLE HistoryTable)"],
        notes="""\
- Status `partial`, `RT-TRAPS: NULL ImageBase`. The shim: `ImageBase` NULL is
  `RT_UNIMPLEMENTED` (Windows writes through it); `HistoryTable` is ignored (it is a cache the
  caller may pass; say so in the header comment); then
  `return (PRUNTIME_FUNCTION)rt_unwind_lookup(ControlPc, ImageBase);`. The core takes the
  loader's lock itself: the shim holds none. Last error is never touched.
- Tests: an address in no module (`0x10`) gives NULL; the address of a native module's
  base (`rt_module_handle_w(u"kernel32.dll")->base`, from `rt/module.h`) gives NULL, because
  our own modules have no unwind data; in both, `*ImageBase` holds what the core's comment
  says for a failed look-up (preset it to a mark first and check against the comment, not
  against a guess); a non-NULL `HistoryTable` changes nothing; the NULL stop with `T_TRAPS`.
  A successful look-up needs a lifted module with unwind data, which a shim test does not
  have: say in `RT-CASES` that success is the core's test's (`core/unwind_corpus`).""",
    ),
    dict(
        id="unwind-step", lib="kernel32", members=["RtlVirtualUnwind"], needs=["rt-unwind"],
        header="rt/unwind.h", core=["rt_unwind_step"], model="CloseHandle",
        protos=["PEXCEPTION_ROUTINE RtlVirtualUnwind(DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc, "
                "PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID *HandlerData, "
                "PDWORD64 EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)"],
        notes="""\
- Status `partial`. The shim, in this order, each a `RT_UNIMPLEMENTED` that prints the value:
  `FunctionEntry`, `ContextRecord`, `HandlerData`, `EstablisherFrame` NULL (Windows
  dereferences all four); `HandlerType` above 2. Then `return rt_unwind_step(HandlerType,
  ImageBase, ControlPc, FunctionEntry, ContextRecord, (uint64_t *)HandlerData,
  EstablisherFrame, ContextPointers);` (`ContextPointers` may be NULL). `RT-TRAPS: NULL
  FunctionEntry; NULL ContextRecord; NULL HandlerData; NULL EstablisherFrame; HandlerType
  above 2`. Last error is never touched.
- One real step in the test, on data the test makes by hand (guest addresses are ordinary
  addresses here, so local arrays serve). `static uint8_t img[0x300];` zeroed is the "image":
  `ImageBase = (DWORD64)(uintptr_t)img`. Code bytes: `memset(img + 0x100, 0x90, 16);` (sixteen
  `nop`: not an epilogue). Unwind data at `img + 0x200`: the four bytes `01 00 00 00`
  (version 1, no flags, prologue size 0, no codes, no frame register). The function entry:
  `RUNTIME_FUNCTION rf = { 0x100, 0x110, 0x200 };` (begin, end, unwind data: check the field
  names in `rt/win/exception.h`). A stack: `uint64_t stack[4] = { 0x1234567890, 0, 0, 0 };`. A
  `CONTEXT c` zeroed with `c.Rsp = (DWORD64)(uintptr_t)stack; c.Rip = ImageBase + 0x104;`.
  Call with `HandlerType` 0, `ControlPc = c.Rip`, `&rf`, `&c`, a `PVOID hd` and a `DWORD64 ef`
  preset to marks, `ContextPointers` NULL. Afterwards: the return value is NULL (no
  handler), `c.Rip == 0x1234567890`, `c.Rsp == (DWORD64)(uintptr_t)(stack + 1)`, and `ef` is
  the frame the core's comment says (for a function with no frame register and no
  allocation, the `Rsp` before the step).
- Then the five stops with `T_TRAPS`, each with the other arguments valid as above.""",
    ),
    dict(
        id="unwind-ex", lib="kernel32", members=["RtlUnwindEx"], needs=["rt-unwind"],
        header=None, core=[], model="Sleep",
        protos=["void RtlUnwindEx(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, "
                "PVOID ReturnValue, PCONTEXT ContextRecord, PUNWIND_HISTORY_TABLE HistoryTable)"],
        notes="""\
- Status `trap` (`docs/RUNTIME.md` section 9.3: a transfer of control, which is what a
  `longjmp` and every C++ `catch` do, is deliberately not built). `RT-CASES: none`,
  `RT-TRAPS: every call`. `runtime/tests/abi/rttest/TrapOnly.c` is a shim of this status.
- The body is one `RT_UNIMPLEMENTED` whose text helps whoever reads the report, and nothing
  follows it (the macro does not return):
  `RT_UNIMPLEMENTED(ctx, "RtlUnwindEx: a transfer of control (longjmp or a C++ catch) to frame
  %p, address %p, exception code 0x%08x", TargetFrame, TargetIp, (unsigned)(ExceptionRecord ?
  ExceptionRecord->ExceptionCode : 0));` (`EXCEPTION_RECORD` is in `rt/win/exception.h`;
  0x80000026 is a `longjmp`, 0xE06D7363 a C++ exception: say so in the shim's comment).
- The test is two statements, with no `T_CASE`: a call with `ExceptionRecord` NULL
  (`T_TRAPS(..., "RtlUnwindEx: a transfer of control")`), and a call with an
  `EXCEPTION_RECORD` (zeroed, then its code set to `0x80000026`):
  `T_TRAPS(..., "exception code 0x80000026")`.""",
    ),
    dict(
        id="pc-to-file-header", lib="kernel32", members=["RtlPcToFileHeader"], needs=["core-loader-query-body"],
        header="rt/module.h", core=["rt_module_from_pc"], model="CloseHandle",
        protos=["PVOID RtlPcToFileHeader(PVOID PcValue, PVOID *BaseOfImage)"],
        notes="""\
- The shim: `BaseOfImage` NULL is `RT_UNIMPLEMENTED` (not documented). Otherwise `RtModule *m =
  rt_module_from_pc((uint64_t)(uintptr_t)PcValue); PVOID base = m ? (PVOID)(uintptr_t)m->base :
  NULL; *BaseOfImage = base; return base;`. Last error is never touched.
- Why this is not left a trap stub (from memory of the Visual Studio 2012 C runtime, not
  verified): its `_CxxThrowException` calls this function to find the image of the throw
  information before it calls `RaiseException`, and the report of a C++ throw needs that base.
- Tests: kernel32's base as the address gives kernel32's base, stored and returned (find the
  module with `rt_module_find("kernel32.dll")`); an address in no module (`(PVOID)0x10`) gives
  NULL, stored and returned; last error left alone in both; the NULL stop with `T_TRAPS`.""",
    ),
    # ---- (a) exceptions and process exit ----
    dict(
        id="uef-set", lib="kernel32", members=["SetUnhandledExceptionFilter"], needs=["core-crash-exception-body"],
        header="rt/crash.h", core=["rt_uef_set", "rt_uef_get"], model="GetTickCount",
        protos=["LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter("
                "LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)"],
        notes="""\
- One core call; a filter is a guest code address and is never called. No failure, no last error.
- Tests: the call returns the filter that was stored before (`rt_uef_get()` before the call is
  the oracle) and stores the new one (`rt_uef_get()` after); storing 0; two calls in a row give
  the first value back; last error left alone. Any non-zero number serves as a filter
  (`(LPTOP_LEVEL_EXCEPTION_FILTER)0x140001000`); store the original value again at the end.""",
    ),
    dict(
        id="raise-exception", lib="kernel32", members=["RaiseException"], needs=["core-crash-exception-body"],
        header="rt/crash.h", core=["rt_raise_exception"], model="Sleep",
        protos=["void RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, "
                "DWORD nNumberOfArguments, const ULONG_PTR *lpArguments)"],
        notes="""\
- One core call. Status `full`, `RT-TRAPS: none`: ending the run with the report for every code
  but three is the specified behaviour here (plan A, `docs/RUNTIME.md` section 9.3), not a
  missing case. One of the `RT-CASES` is that report, and its `T_CASE` checks it with `T_TRAPS`.
- Tests: `DBG_PRINTEXCEPTION_C` (0x40010006) with arguments `{ length with the NUL, pointer to
  "hello\\n" }` returns and logs `hello` (`T_LOGGED(RT_CH_GUEST_DEBUG, "hello")`); the thread-name
  code 0x406D1388 with `{ 0x1000, pointer to a name, 0xFFFFFFFF }` returns; a malformed one
  (no arguments) returns; last error is left alone by all three; the C++ code 0xE06D7363 and
  0xC0000005 end the run: `T_TRAPS(CALL(kernel32, RaiseException, 0xE06D7363, 1, 0, NULL),
  "FATAL guest raised exception 0x")` (match no more of the line than that).""",
    ),
    dict(
        id="process-end", lib="kernel32", members=["TerminateProcess", "GetExitCodeProcess"],
        needs=["core-crash-exit-body"], header="rt/crash.h",
        core=["rt_process_terminate", "rt_process_exit_code"], model="CloseHandle",
        protos=["BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode)",
                "BOOL GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)"],
        notes="""\
- One core call each; the functions of `rt/crash.h` set last error themselves when they fail.
  `GetExitCodeProcess` is `partial` with `RT-TRAPS: core: lpExitCode NULL` (the core stops
  there; add no guard, prove it with `T_TRAPS`). `TerminateProcess` is `full`.
- A call that ends the process is tested in a child: `rt/test_child.h` (read its comment, 25
  lines) gives `rt_test_child_run`, `T_CHILD_EXITED` and `T_CHILD_SAID`.
- Tests for `TerminateProcess`: NULL, a handle that is not open and an event handle
  (`rt_event_create(T_CTX(), FALSE, FALSE, NULL)`, `rt/sync.h`) return FALSE with
  `ERROR_INVALID_HANDLE`, in the test's own process (at least one such call must be there:
  the check counts calls in the test's process only); `(HANDLE)-1` with code 7 in a child:
  `T_CHILD_EXITED(&c, 7)` and `T_CHILD_SAID(&c, "process exit: TerminateProcess(0x00000007)")`;
  a code of 0x100 in a child exits with status 255.
- Tests for `GetExitCodeProcess`: `(HANDLE)-1` gives TRUE and 259 (`STILL_ACTIVE`), last error
  left alone; NULL and an event handle give FALSE, `ERROR_INVALID_HANDLE`, and the DWORD is
  not written.""",
    ),
    dict(
        id="process-exit", lib="kernel32", members=["ExitProcess", "FatalAppExitA"], needs=["core-crash-exit-body"],
        header="rt/crash.h", core=["rt_process_exit", "rt_fatal_app_exit"], model="Sleep",
        protos=["void ExitProcess(UINT uExitCode)", "void FatalAppExitA(UINT uAction, LPCSTR lpMessageText)"],
        notes="""\
- One core call each, which does not return: `rt_process_exit(ctx, uExitCode);` and
  `rt_fatal_app_exit(ctx, uAction, lpMessageText);`. Status `full`, `RT-TRAPS: none`.
- Every call is tested in a child: `rt/test_child.h` (read its comment, 25 lines) gives
  `rt_test_child_run`, `T_CHILD_EXITED` and `T_CHILD_SAID`. Never call either function in the
  test's own process.
- Tests for `ExitProcess`: code 0 and code 7 exit with that status and the child said
  `process exit: ExitProcess(0x00000007)`; a code of 0xC0000409 exits with status 255.
- Tests for `FatalAppExitA`: the child exits with status 3 and said `FATAL guest called
  FatalAppExit: <the message>`; a NULL message prints `(null)`.""",
    ),
    dict(
        id="uef-unhandled", lib="kernel32", members=["UnhandledExceptionFilter"],
        needs=["core-crash-exception-body"], header="rt/crash.h", core=["rt_uef_unhandled"], model="Sleep",
        protos=["LONG UnhandledExceptionFilter(EXCEPTION_POINTERS *ExceptionInfo)"],
        notes="""\
- One core call, which does not return: `rt_uef_unhandled(ctx, (uint64_t)(uintptr_t)ExceptionInfo);`.
  Status `full`, `RT-TRAPS: none`: the report is the specified behaviour (plan A).
- Tests, each with `T_TRAPS(CALL(kernel32, UnhandledExceptionFilter, p), "FATAL guest called
  UnhandledExceptionFilter")`: NULL (the report says `no EXCEPTION_POINTERS`); a pointer to an
  `EXCEPTION_POINTERS` whose record has code 0xC0000005 (`rt/win/exception.h`).""",
    ),
    # ---- (a) fixed answers: no console, the ANSI file functions, the heap check ----
    dict(
        id="console-mode", lib="kernel32", members=["GetConsoleMode", "GetConsoleCP"], needs=["rt-contract-file"],
        header=None, core=[], model="GetTickCount",
        protos=["BOOL GetConsoleMode(HANDLE hConsoleHandle, LPDWORD lpMode)", "UINT GetConsoleCP(void)"],
        notes=ABSENT + """\
- `GetConsoleMode`: `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);` for every handle, the
  standard handles included; `*lpMode` is not written. (From memory: the C runtime asks this
  for a character device before each write, and FALSE makes it write with `WriteFile`.)
- `GetConsoleCP`: `return FAIL(ctx, ERROR_INVALID_HANDLE, 0);` (0 is what Windows returns
  without a console).
- Tests: `GetConsoleMode` with NULL, `(HANDLE)0x44` and a NULL `lpMode`; a DWORD preset to
  0xA5A5A5A5 keeps its value; both last errors; both log lines.""",
    ),
    dict(
        id="console-io", lib="kernel32", members=["WriteConsoleW", "ReadConsoleW"], needs=["rt-contract-file"],
        header=None, core=[], model="GetTickCount",
        protos=["BOOL WriteConsoleW(HANDLE hConsoleOutput, LPCVOID lpBuffer, DWORD nNumberOfCharsToWrite, "
                "LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)",
                "BOOL ReadConsoleW(HANDLE hConsoleInput, LPVOID lpBuffer, DWORD nNumberOfCharsToRead, "
                "LPDWORD lpNumberOfCharsRead, LPVOID pInputControl)"],
        notes=ABSENT + """\
- Both: `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);`. Neither count is written and no
  buffer is read or written. `pInputControl` is `LPVOID` here (the documentation's
  `PCONSOLE_READCONSOLE_CONTROL` is not defined in this runtime and is never looked at).
- Tests: each with a handle of `(HANDLE)0x44` and with every pointer NULL; a count preset to
  0xA5A5A5A5 and a buffer preset to a pattern keep their values; the last error; the log line.""",
    ),
    dict(
        id="alloc-console", lib="kernel32", members=["AllocConsole"], needs=["rt-contract-file"],
        header=None, core=[], model="GetTickCount",
        protos=["BOOL AllocConsole(void)"],
        notes=ABSENT + """\
- `return FAIL(ctx, ERROR_ACCESS_DENIED, FALSE);`: the runtime never makes a console (the file
  contract's decision; on Windows this is the code for a process that already has one).
- Tests: FALSE, the last error, the log line, twice in a row.""",
    ),
    dict(
        id="file-apis-ansi", lib="kernel32", members=["AreFileApisANSI"], needs=["rt-contract-file"],
        header=None, core=[], model="GetTickCount",
        protos=["BOOL AreFileApisANSI(void)"],
        notes="""\
- `return TRUE;`, status `full`, `RT-TRAPS: none`. The source: every A file function of this
  runtime reads a name as the ANSI code page, 1252 (`rt/file.h`, NAMES; `rt_path_to_host_a`), and
  nothing can switch them to the OEM code page (`SetFileApisToOEM` has no shim and no caller).
- Tests: TRUE, twice; last error is left alone.""",
    ),
    dict(
        id="heap-validate", lib="kernel32", members=["HeapValidate"], needs=["rt-core-heap"],
        header="rt/heap.h", core=["rt_heap_validate", "rt_heap_process", "rt_heap_alloc", "rt_heap_free"],
        model="GetTickCount",
        protos=["BOOL HeapValidate(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)"],
        notes=CORE_STOPS + """\
- One core call, `return rt_heap_validate(hHeap, dwFlags, lpMem);`. Last error is never set.
- Tests, on the process heap (`rt_heap_process()`), with blocks from `rt_heap_alloc` that the
  test frees with `rt_heap_free`: a live block is TRUE; the same block after it was freed, an
  address 8 bytes into a live block, and the address of a local variable are FALSE; a NULL
  block is TRUE; a NULL heap handle is FALSE; `HEAP_NO_SERIALIZE` (1) is accepted; last error is
  left alone by all of them; the stop `core: a flag bit other than HEAP_NO_SERIALIZE` (flags 2).""",
    ),
    # ---- (b) the machine, as a platform layer asks for it ----
    dict(
        id="version-ex", lib="kernel32", members=["GetVersionExA"], needs=["rt-core-machine"],
        header="rt/machine.h", core=["rt_machine"], model="GetVersion",
        protos=["BOOL GetVersionExA(LPOSVERSIONINFOA lpVersionInformation)"],
        notes="""\
- The structures `OSVERSIONINFOA` (148 bytes) and `OSVERSIONINFOEXA` (156 bytes, the same six
  members first) and the `VER_` constants are in `rt/win/sysinfo.h`: include it. The caller
  says which one it passes in `dwOSVersionInfoSize`, which the shim reads and does not write.
- 148: fill the `OSVERSIONINFOA`. 156: fill the `OSVERSIONINFOEXA` (cast the pointer). Any other
  size: `RT_UNIMPLEMENTED` with the size (the documentation says the call fails and names no
  error code). A NULL pointer: `RT_UNIMPLEMENTED`. Status `partial`.
- The values, from `const RtMachine *m = rt_machine();` (Windows 7 SP1, `docs/RUNTIME.md`
  section 8.5; the same numbers `GetVersion` and the process block report):
  `dwMajorVersion = m->os_major` (6), `dwMinorVersion = m->os_minor` (1), `dwBuildNumber =
  m->os_build` (7601), `dwPlatformId = VER_PLATFORM_WIN32_NT`, `szCSDVersion` = all 128 bytes
  zero and then the text `m->sp_name` ("Service Pack 1"). For 156 also `wServicePackMajor =
  m->sp_major` (1), `wServicePackMinor = m->sp_minor` (0), `wSuiteMask = VER_SUITE_SINGLEUSERTS`
  (planner's choice, from memory of what Windows 7 Professional reports), `wProductType =
  VER_NT_WORKSTATION`, `wReserved = 0`.
- Returns TRUE; last error is left alone.
- Tests: fill a 156-byte buffer with 0xA5 and set the size before each call. Size 148: every
  member, all 128 bytes of `szCSDVersion`, the size member unchanged, and the 8 bytes after the
  148 still 0xA5. Size 156: every member. Agreement: `CALL(kernel32, GetVersion)` has the major
  version in its low byte, the minor in the next and the build in its high word. Last error
  left alone. The stops: sizes 0 and 152, and NULL, each with `T_TRAPS`.""",
    ),
    dict(
        id="memory-status", lib="kernel32", members=["GlobalMemoryStatusEx"], needs=["rt-core-machine"],
        header="rt/machine.h", core=["rt_machine"], model="GetVersion",
        protos=["BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX lpBuffer)"],
        notes="""\
- `MEMORYSTATUSEX` (64 bytes) is in `rt/win/sysinfo.h`: include it. The caller sets `dwLength`
  to 64, which the shim reads and does not write. Another `dwLength`, or a NULL pointer:
  `RT_UNIMPLEMENTED` (the documentation says the member "must" be set and names no error code).
  Status `partial`.
- The values, from `const RtMachine *m = rt_machine();` (`docs/RUNTIME.md` section 8.5: host
  memory as total, half of it as available, 8 TB of address space):
  `ullTotalPhys = m->phys_total`, `ullAvailPhys = m->phys_avail`,
  `dwMemoryLoad = (DWORD)((m->phys_total - m->phys_avail) * 100 / m->phys_total)`,
  `ullTotalPageFile = m->phys_total` and `ullAvailPageFile = m->phys_avail` (planner's choice:
  the commit limit is physical memory, as on a machine with no paging file),
  `ullTotalVirtual = m->max_address - m->min_address + 1`,
  `ullAvailVirtual = ullTotalVirtual - (m->phys_total - m->phys_avail)` (planner's choice: the
  runtime keeps no count of reserved address space, and the answer must be below the total),
  `ullAvailExtendedVirtual = 0` (documented: "Reserved. This value is always 0").
- Returns TRUE; last error is left alone. The answer is the same on every call.
- Tests: fill a buffer of 72 bytes with 0xA5 and set `dwLength`: every member against
  `rt_machine()`, `dwLength` still 64, `dwMemoryLoad` at most 100, the 8 bytes after the
  structure still 0xA5, last error left alone; the stops for `dwLength` 0 and 32 and for NULL.""",
    ),
    dict(
        id="logical-cpus", lib="kernel32", members=["GetLogicalProcessorInformation"], needs=["rt-core-machine"],
        header="rt/machine.h", core=["rt_machine", "rt_machine_system_info"], model="GetVersion",
        protos=["BOOL GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, "
                "PDWORD ReturnedLength)"],
        notes="""\
- `SYSTEM_LOGICAL_PROCESSOR_INFORMATION` (32 bytes) and the `Relation` values are in
  `rt/win/sysinfo.h`: include it. The machine (`docs/RUNTIME.md` section 8.5) has
  `n = rt_machine()->cpu_count` processors (1 to 64), one package, no SMT.
- The records, n + 2 of them, in this order (the documentation fixes none), every byte that is
  not named being 0:
  n records `RelationProcessorCore`, record i with `ProcessorMask = 1 << i` (64-bit) and
  `ProcessorCore.Flags = 0` (the processors of the mask do not share a core);
  one `RelationNumaNode` with the mask of all n processors and `NumaNode.NodeNumber = 0` (the
  documentation: a system without NUMA reports a single record of this type);
  one `RelationProcessorPackage` with the mask of all n processors.
  The mask of all n is `n == 64 ? ~0ull : (1ull << n) - 1`. There is **no** `RelationCache`
  record: the `cpuid` this runtime shows the guest describes no cache, and the two must agree
  (planner's decision).
- `needed = (n + 2) * 32`. `ReturnedLength` NULL: `RT_UNIMPLEMENTED` (not documented). If
  `*ReturnedLength < needed`: store `needed` in it, write nothing to the buffer, and
  `return FAIL(ctx, ERROR_INSUFFICIENT_BUFFER, FALSE);` (documented; `Buffer` may be NULL here).
  Otherwise `Buffer` NULL is `RT_UNIMPLEMENTED`; else write the records, store `needed`, return
  TRUE and leave last error alone. Status `partial`.
- Tests: the size query (NULL, length 0); a buffer 1 byte too small, untouched (preset to
  0xA5); an exact buffer and one 32 bytes larger whose last 32 bytes stay 0xA5; every byte of
  every record; the number of core records and the package mask agree with
  `rt_machine_system_info` (`dwNumberOfProcessors`, `dwActiveProcessorMask`); last error left
  alone on success; the two stops with `T_TRAPS`.""",
    ),
    dict(
        id="time-zone", lib="kernel32", members=["GetTimeZoneInformation"], needs=["rt-core-time"],
        header="rt/time.h", core=["rt_time_zone_bias_minutes"], model="GetTickCount",
        protos=["DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation)"],
        notes="""\
- `TIME_ZONE_INFORMATION` (172 bytes) and the `TIME_ZONE_ID_` values are in
  `rt/win/sysinfo.h`: include it.
- The shim: a NULL pointer is `RT_UNIMPLEMENTED` (not documented). Otherwise set all 172 bytes
  to 0, then `Bias = rt_time_zone_bias_minutes()`, and return `TIME_ZONE_ID_UNKNOWN` (0), which
  the documentation gives for a zone with no transition dates. So `StandardBias` and
  `DaylightBias` are 0 (the core's comment), both dates are all zero (month 0: no transition)
  and both names are empty (planner's choice; the structure's page allows an empty name, from
  memory). Last error is left alone. Status `partial`.
- Tests: fill 180 bytes with 0xA5: the return value; `Bias` equals the core's value; the other
  168 bytes of the structure are 0; the 8 bytes after it still 0xA5; last error left alone; the
  NULL stop with `T_TRAPS`.""",
    ),
    # ---- (b) threads and the process (core: rt-contract-thread) ----
    dict(
        id="thread-priority", lib="kernel32", members=["GetThreadPriority", "SetThreadPriority"],
        needs=["core-thread-ctl-body"], header="rt/thread.h",
        core=["rt_thread_set_priority", "rt_thread_get_priority"], model="CloseHandle",
        protos=["int GetThreadPriority(HANDLE hThread)", "BOOL SetThreadPriority(HANDLE hThread, int nPriority)"],
        notes=THREAD_ERR + CORE_STOPS + """\
- One core call each. The `THREAD_PRIORITY_` values are in `rt/win/process.h`.
- Tests, on the calling thread's pseudo-handle `(HANDLE)-2`: it starts at 0; each of the seven
  values is stored and read back; NULL and an event handle (`rt_event_create(T_CTX(), FALSE,
  FALSE, NULL)`, `rt/sync.h`) give FALSE, and `THREAD_PRIORITY_ERROR_RETURN` from the getter,
  with `ERROR_INVALID_HANDLE`; success leaves last error alone; the stop `core: a priority that
  is not one of the seven` (3). Set the priority back to 0 at the end.""",
    ),
    dict(
        id="thread-info", lib="kernel32", members=["GetExitCodeThread", "OpenThread"],
        needs=["core-thread-ctl-body"], header="rt/thread.h",
        core=["rt_thread_get_exit_code", "rt_thread_open"], model="CloseHandle",
        protos=["BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)",
                "HANDLE OpenThread(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId)"],
        notes=THREAD_ERR + CORE_STOPS + """\
- One core call each. `OpenThread` ignores the access mask and the inherit flag (no access
  checks and no child processes in this runtime): say so in a comment.
- Tests, without starting a thread (the core's own tests cover a thread that has ended):
  `OpenThread` with the test's own id (`CALL(kernel32, GetCurrentThreadId)`) gives a handle,
  with last error left alone, and `GetExitCodeThread` on it and on `(HANDLE)-2` gives TRUE and
  259 (`STILL_ACTIVE`); an id no thread has (0x7FFFFFF0) gives NULL and
  `ERROR_INVALID_PARAMETER`; NULL and an event handle give FALSE and `ERROR_INVALID_HANDLE`
  with the DWORD not written; the stop `core: lpExitCode NULL`. Close what `OpenThread`
  returned with `rt_handle_close` (`rt/handle.h`).""",
    ),
    dict(
        id="thread-affinity", lib="kernel32", members=["SetThreadAffinityMask"], needs=["core-thread-ctl-body"],
        header="rt/thread.h", core=["rt_thread_set_affinity", "rt_thread_open"], model="CloseHandle",
        protos=["DWORD_PTR SetThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask)"],
        notes=THREAD_ERR + """\
- One core call. The system mask has the bits 0 to `rt_machine()->cpu_count - 1` (`rt/machine.h`).
- Tests, on a handle to the test's own thread (`rt_thread_open(CALL(kernel32,
  GetCurrentThreadId))`, closed with `rt_handle_close` at the end): the first call returns the
  system mask (the process's mask, which this test must not change) and a second call returns
  what the first stored; a mask of 0, and a mask with bit 63 set when `cpu_count` is below 64,
  return 0 with `ERROR_INVALID_PARAMETER`; NULL and an event handle return 0 with
  `ERROR_INVALID_HANDLE`; success leaves last error alone. Store the system mask again at the end.""",
    ),
    dict(
        id="process-affinity", lib="kernel32", members=["GetProcessAffinityMask", "SetProcessAffinityMask"],
        needs=["core-process-body"], header="rt/thread.h",
        core=["rt_process_get_affinity", "rt_process_set_affinity"], model="CloseHandle",
        protos=["BOOL GetProcessAffinityMask(HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, "
                "PDWORD_PTR lpSystemAffinityMask)",
                "BOOL SetProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask)"],
        notes=THREAD_ERR + CORE_STOPS + """\
- One core call each. The system mask has the bits 0 to `rt_machine()->cpu_count - 1`
  (`rt/machine.h`); the process's mask starts as the system mask.
- Tests, on the pseudo-handle `(HANDLE)-1`: both masks are the system mask at first; a mask of
  bit 0 alone is stored and read back while the system mask stays; a mask of 0, and one with
  bit 63 set when `cpu_count` is below 64 (`ERROR_INVALID_PARAMETER`, the stored mask
  unchanged); NULL and an event handle (`ERROR_INVALID_HANDLE`, the two DWORD_PTRs not
  written); success leaves last error alone; the stop `core: a NULL mask pointer`. Store the
  system mask again at the end, and do not set a thread's mask in this test.""",
    ),
    dict(
        id="priority-class", lib="kernel32", members=["SetPriorityClass"], needs=["core-process-body"],
        header="rt/thread.h", core=["rt_process_set_priority_class", "rt_process_get_priority_class"],
        model="CloseHandle",
        protos=["BOOL SetPriorityClass(HANDLE hProcess, DWORD dwPriorityClass)"],
        notes=THREAD_ERR + CORE_STOPS + """\
- One core call. The `_PRIORITY_CLASS` values and `PROCESS_MODE_BACKGROUND_BEGIN` are in
  `rt/win/process.h`.
- Tests, on `(HANDLE)-1`, reading back with `rt_process_get_priority_class`: each of the six
  classes; a value that is no class (0x1234: `ERROR_INVALID_PARAMETER`, the class unchanged);
  NULL and an event handle (`ERROR_INVALID_HANDLE`); success leaves last error alone; the stop
  `core: PROCESS_MODE_BACKGROUND_BEGIN`. Set `NORMAL_PRIORITY_CLASS` again at the end.""",
    ),
    dict(
        id="thread-stop", lib="kernel32", members=["SuspendThread", "TerminateThread"], needs=["rt-contract-thread"],
        header=None, core=[], model="Sleep",
        protos=["DWORD SuspendThread(HANDLE hThread)", "BOOL TerminateThread(HANDLE hThread, DWORD dwExitCode)"],
        notes="""\
- Status `trap` (`docs/RUNTIME.md` section 6.3 and `rt/thread.h`: stopping another guest thread
  is deliberately not built until a caller is seen). `RT-CASES: none`, `RT-TRAPS: every call`.
  The whole body is one line and nothing follows it (the macro does not return):
  `RT_UNIMPLEMENTED(ctx, "SuspendThread(%p)", hThread);` and
  `RT_UNIMPLEMENTED(ctx, "TerminateThread(%p, %u)", hThread, dwExitCode);`.
  `runtime/tests/abi/rttest/TrapOnly.c` is a shim of this status.
- Each test is one statement, with no `T_CASE`:
  `T_TRAPS(CALL(kernel32, SuspendThread, (HANDLE)(uintptr_t)0x44), "SuspendThread(0x44)");` and
  `T_TRAPS(CALL(kernel32, TerminateThread, (HANDLE)(uintptr_t)0x44, 7), "TerminateThread(0x44, 7)");`.""",
    ),
    # ---- (c) files by name (core: rt-contract-file) ----
    dict(
        id="find-file", lib="kernel32", members=["FindFirstFileA", "FindNextFileA"], needs=["core-file-find-body"],
        header="rt/file.h", core=["rt_find_first_w", "rt_find_next_w", "rt_find_first_a", "rt_find_next_a", "rt_find_close"],
        model="CloseHandle",
        protos=["HANDLE FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)",
                "BOOL FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)"],
        notes=FILE_ERR + FILE_TEST + CORE_STOPS + """\
- One core call each: `rt_find_first_a(lpFileName, lpFindFileData)` and `rt_find_next_a`. The
  comment of the W forms is the specification of both; the A forms differ as their comment says.
- Tests: make a directory under the install root with two files and a sub-directory (host
  `mkdir` and `put`); the pattern `C:\\Titanfall2\\d\\*` gives `.`, `..` and then the three names in
  the order the core's comment gives, each with its attributes and size, and after the last
  one FALSE with `ERROR_NO_MORE_FILES`, twice; `*.txt` gives only the matching file; a pattern
  that matches nothing (`INVALID_HANDLE_VALUE`, `ERROR_FILE_NOT_FOUND`); a directory that does
  not exist (`ERROR_PATH_NOT_FOUND`); `FindNextFileA` on NULL and on a file handle
  (`ERROR_INVALID_HANDLE`); success leaves last error alone; the stop `core: lpFindFileData
  NULL` for each function. Close every find handle with `rt_find_close`.""",
    ),
    dict(
        id="find-close", lib="kernel32", members=["FindClose"], needs=["core-file-find-body"],
        header="rt/file.h", core=["rt_find_close", "rt_find_first_w", "rt_find_next_w"], model="CloseHandle",
        protos=["BOOL FindClose(HANDLE hFindFile)"],
        notes=FILE_ERR + FILE_TEST + """\
- One core call. Tests: a handle from `rt_find_first_w` on `C:\\Titanfall2\\*` is closed (TRUE,
  last error left alone), after which `rt_find_next_w` on it and a second `FindClose` fail with
  `ERROR_INVALID_HANDLE`; NULL, `INVALID_HANDLE_VALUE` and a file handle fail with
  `ERROR_INVALID_HANDLE`, and the file handle still works afterwards.""",
    ),
    dict(
        id="file-attr", lib="kernel32", members=["GetFileAttributesA", "SetFileAttributesA"],
        needs=["core-file-attr-body"], header="rt/file.h",
        core=["rt_file_get_attributes", "rt_file_set_attributes"], model="CloseHandle",
        protos=["DWORD GetFileAttributesA(LPCSTR lpFileName)",
                "BOOL SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes)"],
        notes=FILE_ERR + FILE_NAME + FILE_TEST + CORE_STOPS + """\
- One core call each. What the core reports (`rt/file.h`, ATTRIBUTES): a directory is exactly
  `FILE_ATTRIBUTE_DIRECTORY`; a file is `FILE_ATTRIBUTE_ARCHIVE`, plus `FILE_ATTRIBUTE_READONLY`
  when its host mode has no owner write bit.
- Tests: a file made with `put(..., 0644)` and one with mode 0444; a directory; a name that
  does not exist (`INVALID_FILE_ATTRIBUTES`, `ERROR_FILE_NOT_FOUND`) and one in a directory that
  does not exist (`ERROR_PATH_NOT_FOUND`); a NULL name (`ERROR_INVALID_PARAMETER`);
  `SetFileAttributesA` with `FILE_ATTRIBUTE_READONLY` and then `FILE_ATTRIBUTE_NORMAL`, seen
  through `GetFileAttributesA` and through the host mode (`stat` on `I(...)`); on a missing
  file; success leaves last error alone; the stop `core: an attribute bit that cannot be set`
  (`FILE_ATTRIBUTE_DIRECTORY`).""",
    ),
    dict(
        id="file-attr-ex", lib="kernel32", members=["GetFileAttributesExA"], needs=["core-file-attr-body"],
        header="rt/file.h", core=["rt_file_get_attributes_ex", "rt_file_get_attributes"], model="CloseHandle",
        protos=["BOOL GetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, "
                "LPVOID lpFileInformation)"],
        notes=FILE_ERR + FILE_NAME + FILE_TEST + CORE_STOPS + """\
- One core call: `rt_file_get_attributes_ex(RT_NAME_A(lpFileName), (DWORD)fInfoLevelId,
  lpFileInformation)`. `WIN32_FILE_ATTRIBUTE_DATA` (36 bytes) and `GetFileExInfoStandard` are in
  `rt/win/file.h`.
- Tests: a file of 5 bytes: attributes as `rt_file_get_attributes` gives them, size 5 in
  `nFileSizeLow` and 0 in `nFileSizeHigh`, a last-write time that is not 0; a directory (size
  0); a level of 1 (`ERROR_INVALID_PARAMETER`); a missing file (`ERROR_FILE_NOT_FOUND`), with
  the 36 bytes untouched in both failures (preset to 0xA5); success leaves last error alone;
  the stop `core: lpFileInformation NULL`.""",
    ),
    dict(
        id="dir-create", lib="kernel32", members=["CreateDirectoryA", "RemoveDirectoryA"],
        needs=["core-file-dir-body"], header="rt/file.h", core=["rt_dir_create", "rt_dir_remove"],
        model="CloseHandle",
        protos=["BOOL CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)",
                "BOOL RemoveDirectoryA(LPCSTR lpPathName)"],
        notes=FILE_ERR + FILE_NAME + FILE_TEST + SEC + CORE_STOPS + """\
- One core call each.
- Tests: a directory is created (the host sees it: `stat` on `I(...)`) and a second create
  fails with `ERROR_ALREADY_EXISTS`, as does a create over a file; a create under a parent that
  does not exist (`ERROR_PATH_NOT_FOUND`); an empty directory is removed; one with a file in it
  is not (`ERROR_DIR_NOT_EMPTY`); a remove of a name that does not exist
  (`ERROR_FILE_NOT_FOUND`); success leaves last error alone; the stop `core: RemoveDirectory on
  a file`.""",
    ),
    dict(
        id="file-delete", lib="kernel32", members=["DeleteFileA", "DeleteFileW"], needs=["core-file-dir-body"],
        header="rt/file.h", core=["rt_file_delete"], model="CloseHandle",
        protos=["BOOL DeleteFileA(LPCSTR lpFileName)", "BOOL DeleteFileW(LPCWSTR lpFileName)"],
        notes=FILE_ERR + FILE_NAME + FILE_TEST + """\
- One core call each.
- Tests, for both forms: a file is deleted (the host no longer has it) and a second delete
  fails with `ERROR_FILE_NOT_FOUND`; a file of host mode 0444 stays and the call fails with
  `ERROR_ACCESS_DENIED`; a directory (`ERROR_ACCESS_DENIED`); a name in a directory that does
  not exist (`ERROR_PATH_NOT_FOUND`); a NULL name (`ERROR_PATH_NOT_FOUND`: Windows's answer by
  Wine's `test_DeleteFileW`, and the core's for this function); success leaves last error
  alone.""",
    ),
    dict(
        id="file-move", lib="kernel32", members=["MoveFileA"], needs=["core-file-dir-body"],
        header="rt/file.h", core=["rt_file_move"], model="CloseHandle",
        protos=["BOOL MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)"],
        notes=FILE_ERR + FILE_NAME + FILE_TEST + CORE_STOPS + """\
- One core call with flags 0: `rt_file_move(RT_NAME_A(lpExistingFileName),
  RT_NAME_A(lpNewFileName), 0)`.
- Tests: a file moves to a new name in the same directory and into a sub-directory (the host
  sees it under the new name only); a directory moves; a target that exists
  (`ERROR_ALREADY_EXISTS`, both files unchanged); a source that does not exist
  (`ERROR_FILE_NOT_FOUND`); a target in a directory that does not exist
  (`ERROR_PATH_NOT_FOUND`); success leaves last error alone; the stop `core: lpNewFileName
  NULL`.""",
    ),
    dict(
        id="drive-type", lib="kernel32", members=["GetDriveTypeW"], needs=["core-file-dir-body"],
        header="rt/file.h", core=["rt_drive_type"], model="GetTickCount",
        protos=["UINT GetDriveTypeW(LPCWSTR lpRootPathName)"],
        notes=FILE_NAME + CORE_STOPS + """\
- One core call: `return rt_drive_type(RT_NAME_W(lpRootPathName));`. Last error is never set.
  The `DRIVE_` values are in `rt/win/file.h`.
- Tests: NULL, `C:\\`, `c:\\` and `C:/` are `DRIVE_FIXED`; `D:\\` and `Z:\\` are
  `DRIVE_NO_ROOT_DIR`; last error left alone by all; the stop `core: a root that is not a drive
  letter, a colon and a slash` (`C:`).""",
    ),
    # ---- (c) the current directory and full paths (core: rt/path.h) ----
    dict(
        id="cwd-w", lib="kernel32", members=["GetCurrentDirectoryW", "SetCurrentDirectoryW"],
        needs=["rt-core-path"], header="rt/path.h", core=["rt_path_get_cwd_w", "rt_path_set_cwd_w"],
        model="GetTickCount",
        protos=["DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)",
                "BOOL SetCurrentDirectoryW(LPCWSTR lpPathName)"],
        notes=PATH_ERR + """\
- `GetCurrentDirectoryW` is `return rt_path_get_cwd_w(lpBuffer, nBufferLength);`: the core's
  return value is the function's (the length written without the terminator, or, when the
  buffer is too small, the size needed with the terminator). What a buffer that is too small
  holds afterwards is the core's to say, not this note's. It does not fail and last error is
  left alone.
- `SetCurrentDirectoryW` is `DWORD e = rt_path_set_cwd_w(lpPathName); if (e) return FAIL(ctx, e,
  FALSE); return TRUE;`, a NULL name included (the core answers it).
- Tests: the install root is the guest's `C:\\Titanfall2`, the host's `rt_test_install_root()`:
  make a sub-directory there with the host's `mkdir`. The directory at the start is what the
  core says (`rt_path_get_cwd_w` is the oracle), returned with its length; the size query
  (length 0, NULL buffer) and a buffer one WCHAR too small both return the length plus one, and
  the small buffer holds exactly what `rt_path_get_cwd_w` leaves in an equal buffer preset the
  same way (the core is the oracle, unit for unit); setting the sub-directory succeeds and the getter then
  gives it, without a backslash at the end; setting `..` goes back up; a directory that does
  not exist, a file, and NULL fail with the code `rt_path_set_cwd_w` returns for the same name,
  and the directory stays what it was; success leaves last error alone. Set the first
  directory again at the end.""",
    ),
    dict(
        id="cwd-a", lib="kernel32", members=["GetCurrentDirectoryA", "SetCurrentDirectoryA"],
        needs=["rt-core-path", "rt-core-locale"], header="rt/path.h",
        core=["rt_path_get_cwd_w", "rt_path_set_cwd_w"], model="CreateEventA",
        more=[("rt/locale.h", ["rt_mb_to_wide", "rt_wide_to_mb"])],
        protos=["DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)",
                "BOOL SetCurrentDirectoryA(LPCSTR lpPathName)"],
        notes=PATH_ERR + A_PATH + """\
- `GetCurrentDirectoryA`: `n = rt_path_get_cwd_w(wide, MAX_PATH)`; `n >= MAX_PATH` (it did not
  fit) is `RT_UNIMPLEMENTED`; convert n + 1 characters into `narrow` as above. Then, as the W
  form: if `nBufferLength < n + 1`, store an empty string (`lpBuffer[0] = 0`) when
  `nBufferLength` is not 0 and `lpBuffer` is not NULL, which is what the core does for the W
  form, and return `n + 1`; else `lpBuffer` NULL is `RT_UNIMPLEMENTED`; else copy the n + 1
  bytes and return n. Last error is left alone.
- `SetCurrentDirectoryA`: a NULL name goes to `rt_path_set_cwd_w(NULL)`; otherwise convert the
  name as above; then `if (e) return FAIL(ctx, e, FALSE); return TRUE;`. Status `partial`.
- Tests, as for the W forms (the guest's `C:\\Titanfall2` is the host's
  `rt_test_install_root()`; make a sub-directory with the host's `mkdir`): the directory at the
  start agrees with `rt_path_get_cwd_w`; the size query; a buffer one byte too small (preset: its
  first byte becomes 0 and no other changes); set a
  sub-directory and read it back; a directory that does not exist and NULL fail with the code
  `rt_path_set_cwd_w` returns for the same name; success leaves last error alone. The stops,
  each with `T_TRAPS`: a name of 300 characters given to the setter; the getter after the
  test set (with `rt_path_set_cwd_w`) a directory whose name has the character U+4E2D (make it
  with the host's `mkdir` and the UTF-8 name "\\xE4\\xB8\\xAD"). Set the first directory again
  at the end.""",
    ),
    dict(
        id="full-path-a", lib="kernel32", members=["GetFullPathNameA"], needs=["rt-core-path", "rt-core-locale"],
        header="rt/path.h", core=["rt_path_full_w"], model="CreateEventA",
        more=[("rt/locale.h", ["rt_mb_to_wide", "rt_wide_to_mb"])],
        protos=["DWORD GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)"],
        notes=PATH_ERR + A_PATH + """\
- The steps: (1) a NULL `lpFileName` goes to the core as NULL; otherwise convert it into
  `in[MAX_PATH]` as above. (2) `e = rt_path_full_w(in, out, MAX_PATH, &len, &part); if (e) return
  FAIL(ctx, e, 0);` with `WCHAR out[MAX_PATH]`. (3) `len > MAX_PATH` (the result did not fit
  `out`) is `RT_UNIMPLEMENTED`. (4) Convert `len + 1` characters of `out` into `narrow` as above.
  (5) If `nBufferLength < len + 1`: return `len + 1`, and write nothing, neither the buffer nor
  `*lpFilePart` (planner's choice for the part; not documented). (6) `lpBuffer` NULL is
  `RT_UNIMPLEMENTED`. (7) Copy the `len + 1` bytes; if `lpFilePart` is not NULL store
  `part == RT_PATH_NO_PART ? NULL : lpBuffer + part`; return `len`. Last error is left alone on
  success. Status `partial`.
- Tests: `x.txt` gives the current directory (`rt_path_get_cwd_w`), a backslash and the name,
  with the part pointing at `x.txt`; `C:\\a\\..\\b\\c.bin` gives `C:\\b\\c.bin`; `C:\\a\\` has a NULL
  part; `lpFilePart` NULL; the size query (0, NULL) and a buffer one byte too small return the
  length plus one and leave a preset buffer and part alone; NULL and `""` return 0 with
  `ERROR_INVALID_NAME`; success leaves last error alone; the stop for a name of 300 characters.""",
    ),
    # ---- (c) file mappings by name, the console handler ----
    dict(
        id="section-a", lib="kernel32", members=["CreateFileMappingA", "OpenFileMappingA"], needs=["rt-file-map"],
        header="rt/file.h", core=["rt_section_create", "rt_section_open"], model="CreateEventA",
        more=[("rt/locale.h", ["rt_mb_to_wide"])],
        protos=["HANDLE CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, "
                "DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName)",
                "HANDLE OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)"],
        notes=SEC + A_NAME + CORE_STOPS + """\
- `rt_section_create(hFile, flProtect, (ULONGLONG)dwMaximumSizeHigh << 32 | dwMaximumSizeLow,
  name)` and `rt_section_open(name)`, with the name converted as above. Both core functions
  set last error themselves, `rt_section_create` on success too (0 or `ERROR_ALREADY_EXISTS`):
  the shim sets it only for a name that does not fit. `OpenFileMappingA` ignores the access
  mask and the inherit flag (say so in a comment).
- Tests (close every handle with `rt_handle_close`, `rt/handle.h`): a section with no file
  (`INVALID_HANDLE_VALUE`, `PAGE_READWRITE`, 65536 bytes) and no name: a handle and last error
  0; with a name, twice: the second gives a handle and `ERROR_ALREADY_EXISTS`;
  `OpenFileMappingA` finds it by name and fails with `ERROR_FILE_NOT_FOUND` for a name nobody
  made; no file and a size of 0 (NULL, `ERROR_INVALID_PARAMETER`); a file handle that is not
  open (`ERROR_INVALID_HANDLE`); a name of `MAX_PATH` characters or more
  (`ERROR_FILENAME_EXCED_RANGE`), for both functions; the stop `core: a protection other than
  the three` (`PAGE_EXECUTE_READ`, in `rt/win/memory.h`).""",
    ),
    dict(
        id="console-ctrl", lib="kernel32", members=["SetConsoleCtrlHandler"], needs=["core-file-std-body"],
        header="rt/file.h", core=["rt_console_ctrl_handler"], model="GetTickCount",
        protos=["BOOL SetConsoleCtrlHandler(PHANDLER_ROUTINE HandlerRoutine, BOOL Add)"],
        notes=CORE_STOPS + """\
- One core call, `return rt_console_ctrl_handler(HandlerRoutine, Add);`. A handler is a guest
  code address that nothing ever calls, so any non-zero number serves in a test
  (`(PHANDLER_ROUTINE)0x140001000`).
- Tests: the four rows of the core's comment, each TRUE with last error left alone; the same
  routine added twice and removed twice; the stop `core: removing a routine that is not in the
  list`. Remove what the test added.""",
    ),
    # ==== sockets: wsock32, ws2_32, iphlpapi (unit rt-contract-net; contract: rt/net.h and
    # docs/handoff/rt-contract-net.md).  One function per row.  Keep this block together. ====
    # ---- stateless helpers: no core, or the built text helpers of core/net.c ----
    dict(
        id="net-htons", lib="wsock32", members=["htons"], needs=["rt-contract-net"], ords={"htons": 9},
        header=None, core=[], model="GetTickCount",
        protos=["USHORT htons(USHORT hostshort)"],
        notes=NET_PURE + """\
- `return (USHORT)(hostshort << 8 | hostshort >> 8);`: the guest is little-endian x64, network
  order is big-endian, so the two bytes swap. Status `full`.
- Tests: 0x1234 gives 0x3412; 0 gives 0; 0xFFFF gives 0xFFFF; 37015 (0x9097, the engine's
  server port) gives 0x9790; last error is left alone (set it to 0xDEAD with `rt_set_last_error
  (T_CTX(), 0xDEAD)` before and compare `T_LAST_ERROR()` after).""",
    ),
    dict(
        id="net-ntohs", lib="wsock32", members=["ntohs"], needs=["rt-contract-net"], ords={"ntohs": 15},
        header=None, core=[], model="GetTickCount",
        protos=["USHORT ntohs(USHORT netshort)"],
        notes=NET_PURE + """\
- The same swap as `htons` (write it out again: a shim does not call another shim):
  `return (USHORT)(netshort << 8 | netshort >> 8);`. Status `full`.
- Tests: 0x3412 gives 0x1234; 0; 0xFFFF; 0x9790 gives 37015; last error is left alone.""",
    ),
    dict(
        id="net-htonl", lib="wsock32", members=["htonl"], needs=["rt-contract-net"], ords={"htonl": 8},
        header=None, core=[], model="GetTickCount",
        protos=["ULONG htonl(ULONG hostlong)"],
        notes=NET_PURE + """\
- The four bytes reversed: `return hostlong >> 24 | (hostlong >> 8 & 0xFF00) | (hostlong << 8 &
  0xFF0000) | hostlong << 24;`. `ULONG` is 32 bits here (the documentation's `u_long`). Status
  `full`.
- Tests: 0x12345678 gives 0x78563412; 0; 0xFFFFFFFF; 0x7F000001 (`INADDR_LOOPBACK`) gives
  0x0100007F; last error is left alone.""",
    ),
    dict(
        id="net-fd-isset", lib="wsock32", members=["__WSAFDIsSet"], needs=["rt-contract-net"],
        ords={"__WSAFDIsSet": 151}, header=None, core=[], model="GetTickCount",
        protos=["int __WSAFDIsSet(SOCKET fd, FD_SET *set)"],
        notes=NET_PURE + """\
- What the `FD_ISSET` macro of winsock2.h calls. 1 if `fd` equals one of the first
  `set->fd_count` entries of `set->fd_array`, else 0 (from memory of Wine's ws2_32; all 64 bits
  of the value are compared). It looks at no socket and needs no `WSAStartup`.
- `set` NULL, or `set->fd_count` above 64 (`RT_WS_FD_SETSIZE`), is `RT_UNIMPLEMENTED`: not
  verified what Windows does. Status `partial`.
- `FD_SET` is the Windows structure of `rt/win/net.h` (`fd_count`, `fd_array`), filled by hand
  in the test; the host's `FD_SET(n, p)` macro is never used.
- Tests: an empty set gives 0; a set of three finds each of the three and not a fourth value;
  an entry beyond `fd_count` is not found; a value that differs only in bit 32 is not found;
  last error is left alone; the two stops.""",
    ),
    dict(
        id="net-inet-addr", lib="wsock32", members=["inet_addr"], needs=["rt-contract-net"],
        ords={"inet_addr": 10}, header="rt/net.h", core=["rt_net_inet_addr"], model="GetTickCount",
        protos=["ULONG inet_addr(const char *cp)"],
        notes=NET_PURE + CORE_STOPS + NET_STOPS + """\
- `return rt_net_inet_addr(cp);`. In `wsock32.dll` this function is ordinal 10 (in `ws2_32.dll`
  it is 11): the `SHIM_ORD` line above is right.
- Tests: the values of the core's comment: "127.0.0.1" gives 0x0100007F (the bytes 7F 00 00 01
  in memory); "1.2.3" gives 0x03000201; "0x7f.1" gives 0x0100007F; NULL, "" and "1.2.3.4.5"
  give `INADDR_NONE`; last error is left alone by every text, and NULL sets `WSAEFAULT` (Wine
  `ws2_32/tests/protocol.c`, `test_inet_addr`; review D27); " " (one space) gives 0 and
  "3.4.5.6  7" gives 3.4.5.6 (D27; until the fix task `winfix-inet-addr` is merged the core
  gives `INADDR_NONE` for " ", sets nothing for NULL and stops for `core: text after a space`).""",
    ),
    dict(
        id="net-inet-pton", lib="ws2_32", members=["inet_pton"], needs=["rt-contract-net"],
        header="rt/net.h", core=["rt_net_pton"], model="GetTickCount",
        protos=["INT inet_pton(INT Family, PCSTR pszAddrString, PVOID pAddrBuf)"],
        notes=NET_ERR + CORE_STOPS + NET_STOPS + """\
- `return rt_net_pton(Family, pszAddrString, pAddrBuf);`. It needs no `WSAStartup`.
- Tests: `AF_INET` "127.0.0.1" gives 1 and the bytes 7F 00 00 01, with the byte after them
  untouched; `AF_INET6` "::FFFF:127.0.0.1" gives 1 and ten zero bytes, FF FF, 7F 00 00 01;
  "::1"; text that is not an address gives 0 with last error left alone (do NOT check the
  buffer after a 0: Windows writes what it had parsed, review D25); family 0 gives -1 and
  `WSAEAFNOSUPPORT`; a NULL string gives -1 and `WSAEFAULT`; "1.1.1.010" (a leading zero) and
  "::%16" (a scope) give 0 (Wine `ws2_32/tests/protocol.c`, `test_inet_pton`; D25; until the
  fix task `winfix-inet-pton` is merged the core stops for the two: `core: a number with a
  leading zero`, `core: a scope`).""",
    ),
    dict(
        id="net-inet-ntop", lib="ws2_32", members=["inet_ntop"], needs=["rt-contract-net"],
        header="rt/net.h", core=["rt_net_ntop"], model="GetTickCount",
        protos=["PCSTR inet_ntop(INT Family, LPCVOID pAddr, PSTR pStringBuf, SIZE_T StringBufSize)"],
        notes=NET_ERR + CORE_STOPS + NET_STOPS + """\
- `return rt_net_ntop(Family, pAddr, pStringBuf, StringBufSize);`. It needs no `WSAStartup`.
  `SIZE_T` is the documentation's `size_t`.
- Tests: `AF_INET` bytes C0 A8 00 01 give "192.168.0.1" and the return value is the buffer;
  `AF_INET6` ::1 gives "::1" and 2001:0db8:0:0:0:0:0:1 gives "2001:db8::1" (RFC 5952); a buffer
  one byte too small gives NULL, last error `(DWORD)STATUS_INVALID_PARAMETER` (0xC000000D, not
  87: Wine `ws2_32/tests/protocol.c`, `test_addr_to_print`; review D26) and an untouched buffer;
  family 0 gives NULL and `WSAEAFNOSUPPORT`; success leaves last error alone; the stop `core: a
  longest zero run of one group` (2001:db8:0:1:1:1:1:1).""",
    ),
    # ---- last error, start and stop, the socket object (core/net.c, built) ----
    dict(
        id="net-get-last-error", lib="wsock32", members=["WSAGetLastError"], needs=["rt-contract-net"],
        ords={"WSAGetLastError": 111}, header=None, core=[], model="GetLastError",
        protos=["int WSAGetLastError(void)"],
        notes="""\
- `return (int)rt_get_last_error(ctx);`: Winsock's error is the thread's last error, the value
  `GetLastError` returns (`rt/net.h`, LAST ERROR: from memory, Wine's ws2_32 forwards this
  function to kernel32's). It works before `WSAStartup` (documented) and changes nothing.
  Status `full`, `RT-TRAPS: none`.
- Tests, with `rt_set_last_error(T_CTX(), x)` to store a value (`rt/shim.h`; do not call the
  `SetLastError` shim): `WSAEWOULDBLOCK` (10035) comes back; so does 0xC0000005 as the int
  -1073741819; two calls in a row give the same value (reading does not clear it); a value
  written straight into the thread block (`ST32(T_CTX()->gs_base + 0x68, 0x12345678)`) is read.""",
    ),
    dict(
        id="net-set-last-error", lib="wsock32", members=["WSASetLastError"], needs=["rt-contract-net"],
        ords={"WSASetLastError": 112}, header=None, core=[], model="SetLastError",
        protos=["void WSASetLastError(int iError)"],
        notes="""\
- `rt_set_last_error(ctx, (DWORD)iError);`: the same value `SetLastError` stores (`rt/net.h`,
  LAST ERROR). Works before `WSAStartup`. Status `full`, `RT-TRAPS: none`.
- Tests: 10035 is stored (`T_LAST_ERROR()`, and `LD32(T_CTX()->gs_base + 0x68)`); 0 is stored
  like any other value (the game's OpenSSL clears the error this way before each send,
  engine+0x2dd663); a negative value is stored as its 32 bits.""",
    ),
    dict(
        id="net-startup", lib="wsock32", members=["WSAStartup"], needs=["rt-contract-net"],
        ords={"WSAStartup": 115}, header="rt/net.h", core=["rt_net_startup", "rt_net_cleanup"], model="GetTickCount",
        protos=["int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)"],
        notes=CORE_STOPS + NET_STOPS + """\
- `return rt_net_startup(wVersionRequested, lpWSAData);`. The result is the error code itself;
  the shim and the core never touch last error (documented).
- Tests (end each successful start with `rt_net_cleanup()`, called directly): 0x0202 gives 0,
  `wVersion` 0x0202, `wHighVersion` 0x0202, `iMaxSockets` 0 and the text "WinSock 2.0" in
  `szDescription`; 0x0101 gives `wVersion` 0x0101 and `wHighVersion` 0x0202; a NULL pointer
  gives `WSAEFAULT` as the return value; 0x0200 (version 0.2) gives `WSAVERNOTSUPPORTED`, with
  a NULL pointer too: the version is looked at first; last error, set to 0xDEAD before, is
  0xDEAD after each of these. What Wine's `ws2_32/tests/protocol.c` (`test_startup`) asserts
  beyond that (review D23): `lpVendorInfo` preset to 0xdeadbeef is not written; a 1.x request
  has `iMaxSockets` 32767 and `iMaxUdpDg` 65467; the structure is filled for version 0.2 as
  well, with `wVersion` 0x0202; 1.2 to 1.255 give 1.1. Until the fix task
  `winfix-wsastartup-versions` is merged the core zero-fills the structure on success only and
  stops for `core: a requested version below 2.2 that is none of the four` (0x0501).""",
    ),
    dict(
        id="net-cleanup", lib="wsock32", members=["WSACleanup"], needs=["rt-contract-net"],
        ords={"WSACleanup": 116}, header="rt/net.h", core=["rt_net_cleanup", "rt_net_startup"], model="GetTickCount",
        protos=["int WSACleanup(void)"],
        notes=NET_ERR + """\
- `return rt_net_cleanup();`. Status `full`.
- Tests (start with `rt_net_startup(0x0202, &data)`, called directly): with no start,
  `SOCKET_ERROR` and `WSANOTINITIALISED`; after two starts, two calls give 0, each with last
  error set to 0 (Wine `ws2_32/tests/protocol.c`, `test_startup`; review D22), and the third
  fails again.""",
    ),
    dict(
        id="net-socket", lib="wsock32", members=["socket"], needs=["rt-contract-net"],
        ords={"socket": 23}, header="rt/net.h", core=["rt_net_socket", "rt_net_close", "rt_net_startup"],
        model="GetTickCount",
        protos=["SOCKET socket(int af, int type, int protocol)"],
        notes=NET_ERR + NET_SETUP + """\
- `return rt_net_socket(af, type, protocol);`. Status `full`.
- Tests: before any start, `INVALID_SOCKET` and `WSANOTINITIALISED`; `(AF_INET6, SOCK_DGRAM,
  IPPROTO_UDP)` and `(AF_INET, SOCK_DGRAM, 0)`, the two the engine makes at start, give two
  different values that are not 0, are multiples of 4 and are below 0x80000000, with last
  error set to 0 (review D22); type 3 gives `WSAESOCKTNOSUPPORT`; `(AF_INET, SOCK_DGRAM,
  IPPROTO_TCP)` gives `WSAEPROTONOSUPPORT`. What Wine's `ws2_32/tests/sock.c` (`test_WSASocket`)
  asserts beyond that (review D24): `(0xdead, 0xdead, IPPROTO_TCP)` gives `WSAESOCKTNOSUPPORT`,
  the type is looked at first; `(AF_INET, 0, IPPROTO_UDP)` is a datagram socket; `(AF_UNSPEC,
  SOCK_DGRAM, 0)` gives `WSAEINVAL`. Until the fix task `winfix-socket-checks` is merged the
  core checks the family first and refuses type 0 and `AF_UNSPEC` (`WSAEAFNOSUPPORT`). Close
  what you opened with `rt_net_close`.""",
    ),
    dict(
        id="net-closesocket", lib="wsock32", members=["closesocket"], needs=["rt-contract-net"],
        ords={"closesocket": 3}, header="rt/net.h", core=["rt_net_close", "rt_net_socket", "rt_net_startup"],
        model="CloseHandle",
        protos=["int closesocket(SOCKET s)"],
        notes=NET_ERR + NET_SETUP + """\
- `return rt_net_close(s);`. Status `full`.
- Tests: a socket of `rt_net_socket` closes with 0 and last error left alone; closing it again
  gives `SOCKET_ERROR` and `WSAENOTSOCK`; so do 0, `INVALID_SOCKET` and the value of an open
  socket with bit 32 set (`s | (SOCKET)1 << 32`), which stays open; before any start a call
  gives `WSANOTINITIALISED`.""",
    ),
    # ---- the fixed answer and the four stops ----
    dict(
        id="net-adapters-info", lib="iphlpapi", members=["GetAdaptersInfo"], needs=["rt-contract-net"],
        header=None, core=[], model="AllocConsole",
        protos=["ULONG GetAdaptersInfo(PVOID AdapterInfo, PULONG SizePointer)"],
        notes="""\
- Status `absent`, signed off in `docs/handoff/rt-contract-net.md` ("The adapter query"): the
  machine has no network adapter (`rt/net.h`, THE MACHINE), and the loopback interface is not
  one this function reports (documented). The answer is the documented one for that machine,
  returned as the result; this function does not use last error.
  `if (!SizePointer) return ERROR_INVALID_PARAMETER;` (documented), otherwise
  `return ERROR_NO_DATA;` (232, `rt/win/net.h`), writing nothing through either pointer.
  `RT-TRAPS: none`. `AdapterInfo` is `PVOID` because no `IP_ADAPTER_INFO` is ever written.
- It logs before the checks: `rt_log_once(RT_CH_NET, RT_LL_DEBUG, "iphlpapi.dll!GetAdaptersInfo",
  "GetAdaptersInfo: the machine has no network adapter");`, and the test checks the line with
  `T_LOGGED(RT_CH_NET, "GetAdaptersInfo: the machine has no network adapter")`.
- Tests: the engine's two calls (engine+0x2175f0): a buffer of 704 bytes preset to 0xAA with a
  size of 0, then with a size of 704: `ERROR_NO_DATA` both times (so not
  `ERROR_BUFFER_OVERFLOW`, 111, which is what would make the engine call again), the size and
  the buffer unchanged; a NULL buffer with a size: `ERROR_NO_DATA`; a NULL size pointer:
  `ERROR_INVALID_PARAMETER`; last error left alone throughout.""",
    ),
    dict(
        id="net-listen", lib="wsock32", members=["listen"], needs=["rt-contract-net"], ords={"listen": 13},
        header=None, core=[], model="Sleep",
        protos=["int listen(SOCKET s, int backlog)"],
        notes=NET_TRAP + """\
- `RT_UNIMPLEMENTED(ctx, "listen(%llu, %d)", (unsigned long long)s, backlog);`
- The test: `T_TRAPS(CALL(wsock32, listen, 68, 5), "listen(68, 5)");`.""",
    ),
    dict(
        id="net-accept", lib="wsock32", members=["accept"], needs=["rt-contract-net"], ords={"accept": 1},
        header=None, core=[], model="Sleep",
        protos=["SOCKET accept(SOCKET s, SOCKADDR *addr, int *addrlen)"],
        notes=NET_TRAP + """\
- `RT_UNIMPLEMENTED(ctx, "accept(%llu)", (unsigned long long)s);`
- The test: `T_TRAPS(CALL(wsock32, accept, 68, NULL, NULL), "accept(68)");`.""",
    ),
    dict(
        id="net-wsaioctl", lib="ws2_32", members=["WSAIoctl"], needs=["rt-contract-net"],
        header=None, core=[], model="Sleep",
        protos=["int WSAIoctl(SOCKET s, DWORD dwIoControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer, "
                "LPVOID lpvOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbBytesReturned, LPVOID lpOverlapped, "
                "GUESTFN lpCompletionRoutine)"],
        notes=NET_TRAP + """\
- `RT_UNIMPLEMENTED(ctx, "WSAIoctl(%llu, 0x%08x)", (unsigned long long)s, dwIoControlCode);`
  (`lpOverlapped` is `LPVOID` and the routine a `GUESTFN`: neither is ever used.)
- The test: `T_TRAPS(CALL(ws2_32, WSAIoctl, 68, 0x98000004u, NULL, 0, NULL, 0, NULL, NULL,
  (GUESTFN)0), "WSAIoctl(68, 0x98000004)");` (the code is `SIO_KEEPALIVE_VALS`, the one the
  game's libcurl passes, engine+0x261f29).""",
    ),
    dict(
        id="net-gethostbyname", lib="wsock32", members=["gethostbyname"], needs=["rt-contract-net"],
        ords={"gethostbyname": 52}, header=None, core=[], model="Sleep",
        protos=["HOSTENT *gethostbyname(const char *name)"],
        notes=NET_TRAP + """\
- `RT_UNIMPLEMENTED(ctx, "gethostbyname(%s)", name ? name : "(null)");`
- The test: `T_TRAPS(CALL(wsock32, gethostbyname, "example.invalid"),
  "gethostbyname(example.invalid)");`.""",
    ),
    # ---- the 16 rows that waited for the five core pieces (docs/handoff/rt-contract-net.md, task
    # table).  The pieces are built (unit rt-net-core, docs/handoff/rt-net-core.md): released. ----
    dict(
        id="net-bind", lib="wsock32", members=["bind"], needs=["rt-net-core"], ords={"bind": 2},
        header="rt/net.h", core=["rt_net_bind", "rt_net_getsockname"], model="GetTickCount",
        protos=["int bind(SOCKET s, const SOCKADDR *name, int namelen)"],
        notes=NET_ERR + NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_bind(s, name, namelen);`.
- Tests, one per line of the core's comment, checking the result through `rt_net_getsockname`:
  the engine's bind (an `AF_INET6` datagram socket to `::` port 37015, `namelen` 28), which
  returns 0 and leaves last error 0 after it was 0xDEAD (review D22); port 0
  gives a port of 49152 or above; a second socket on the same port gets `WSAEADDRINUSE`; an
  address that is not local (192.0.2.1) gets `WSAEADDRNOTAVAIL`; a second bind `WSAEINVAL`;
  `namelen` 27 `WSAEFAULT`; a wrong family `WSAEAFNOSUPPORT`; not a socket `WSAENOTSOCK`.""",
    ),
    dict(
        id="net-getsockname", lib="wsock32", members=["getsockname"], needs=["rt-net-core"],
        ords={"getsockname": 6}, header="rt/net.h",
        core=["rt_net_getsockname", "rt_net_bind"], model="GetTickCount",
        protos=["int getsockname(SOCKET s, SOCKADDR *name, int *namelen)"],
        notes=NET_ERR + NET_SETUP + """\
- `return rt_net_getsockname(s, name, namelen);`.
- Tests: after `rt_net_bind` to 127.0.0.1 port 4000 an `AF_INET` socket reports family 2, the
  port in network order, the address, zero in `sin_zero`, and `*namelen` 16; an `AF_INET6`
  socket bound to `::` port 0 reports `*namelen` 28 and a port that is not 0; an unbound socket
  gets `WSAEINVAL`; a `*namelen` of 15 gets `WSAEFAULT`; not a socket `WSAENOTSOCK`.""",
    ),
    dict(
        id="net-connect", lib="wsock32", members=["connect"], needs=["rt-net-core"], ords={"connect": 4},
        header="rt/net.h", core=["rt_net_connect", "rt_net_getpeername"], model="GetTickCount",
        protos=["int connect(SOCKET s, const SOCKADDR *name, int namelen)"],
        notes=NET_ERR + NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_connect(s, name, namelen);`.
- Tests: a stream socket to 127.0.0.1 port 80 gets `WSAECONNREFUSED`, to 192.0.2.1 port 80
  `WSAENETUNREACH`, the same in non-blocking mode; a datagram socket to 127.0.0.1 port 4000
  gives 0 and `rt_net_getpeername` reports it; connecting it then with a zeroed 16-byte name
  (family `AF_UNSPEC`) gives 0 and `rt_net_getpeername` fails with `WSAENOTCONN`, and a stream
  socket to 0.0.0.0 port 80 gets `WSAEADDRNOTAVAIL` (Wine `ws2_32/tests/sock.c`,
  `test_connect_udp`; review D28); to 192.0.2.1 `WSAENETUNREACH`; the stop `core: a datagram
  socket connected to the unspecified address` (`AF_INET` 0.0.0.0); not a socket `WSAENOTSOCK`.""",
    ),
    dict(
        id="net-getpeername", lib="wsock32", members=["getpeername"], needs=["rt-net-core"],
        ords={"getpeername": 5}, header="rt/net.h",
        core=["rt_net_getpeername", "rt_net_connect"], model="GetTickCount",
        protos=["int getpeername(SOCKET s, SOCKADDR *name, int *namelen)"],
        notes=NET_ERR + NET_SETUP + """\
- `return rt_net_getpeername(s, name, namelen);`.
- Tests: a new datagram socket and a new stream socket get `WSAENOTCONN`, also with a NULL
  name and a NULL length: no peer is reported before the pointers are looked at (Wine
  `ws2_32/tests/sock.c`, `test_getpeername`; review D29); after `rt_net_connect` to 127.0.0.1
  port 4000 the datagram socket reports that address and port with `*namelen` 16; a `*namelen`
  of 15 gets `WSAEFAULT` and stays 15; not a socket `WSAENOTSOCK`.""",
    ),
    dict(
        id="net-setsockopt", lib="wsock32", members=["setsockopt"], needs=["rt-net-core"],
        ords={"setsockopt": 21}, header="rt/net.h",
        core=["rt_net_setsockopt", "rt_net_getsockopt"], model="GetTickCount",
        protos=["int setsockopt(SOCKET s, int level, int optname, LPCVOID optval, int optlen)"],
        notes=NET_ERR + NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_setsockopt(s, level, optname, optval, optlen);`. `optval` is `LPCVOID` (the
  documentation's `const char *`) so that the call trace does not print it as text.
- Tests, read back with `rt_net_getsockopt`: the engine's five calls on a UDP socket
  (engine+0x21ad8d): `SO_SNDBUF` and `SO_RCVBUF` 0x20000, `IPV6_V6ONLY` 0, `SO_REUSEADDR` 1; on a
  TCP socket `SO_KEEPALIVE` 1, `SO_LINGER` {0, 0}, `TCP_NODELAY` 1; `TCP_NODELAY` on a datagram
  socket gets `WSAENOPROTOOPT`; `SO_SNDBUF` with `optlen` 3 `WSAEFAULT`; the sizes of Wine's
  `ws2_32/tests/sock.c` (`test_set_getsockopt`; review D30): `SO_REUSEADDR` with `optlen` 1 gives
  0, and `TCP_NODELAY` set from an int of 0x100 with `optlen` 4 reads back 0 (only the first
  byte counts); `IPV6_V6ONLY` 0 on an `AF_INET` datagram socket gives 0 (`test_ipv6only`; D31);
  a call that succeeds leaves last error 0 (D22); not a socket `WSAENOTSOCK`; the stops `core:
  any other level or option` (level 0xffff, option 0x7777) and `core: SO_LINGER on a datagram
  socket`.""",
    ),
    dict(
        id="net-getsockopt", lib="wsock32", members=["getsockopt"], needs=["rt-net-core"],
        ords={"getsockopt": 7}, header="rt/net.h",
        core=["rt_net_getsockopt", "rt_net_setsockopt"], model="GetTickCount",
        protos=["int getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)"],
        notes=NET_ERR + NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_getsockopt(s, level, optname, optval, optlen);`.
- Tests: the defaults of the core's comment on a new socket (`SO_RCVBUF` 65536, `SO_ERROR` 0,
  `SO_TYPE` the type), each with `*optlen` set to 4 and the bytes after the value untouched;
  the one-byte options (Wine `ws2_32/tests/sock.c`, `test_set_getsockopt`; review D30):
  `TCP_NODELAY` on a stream socket and `IPV6_V6ONLY` on an `AF_INET6` socket, read into an int
  preset to 0xdeadbeef with `*optlen` 4, leave 0xdeadbe00 and 0xdeadbe01 and set `*optlen` to 1;
  `SO_RCVBUF` with `*optlen` 3 gets `WSAEFAULT`; `SO_LINGER` on a datagram socket gets
  `WSAENOPROTOOPT` (`test_extendedSocketOptions`; D32); not a socket `WSAENOTSOCK`; the stop
  `core: any other level or option`. EVERY successful read sets last error to 0 (review D22;
  `SO_ERROR` was `docs/reviews/contracts-check.md`, C11): after
  `rt_thread_set_last_error(10035)` a read of `SO_ERROR` returns 0 with the value 0 and last
  error is then 0, and the same after a read of `SO_TYPE`.""",
    ),
    dict(
        id="net-ioctlsocket", lib="wsock32", members=["ioctlsocket"], needs=["rt-net-core"],
        ords={"ioctlsocket": 12}, header="rt/net.h", core=["rt_net_ioctl"], model="GetTickCount",
        protos=["int ioctlsocket(SOCKET s, LONG cmd, ULONG *argp)"],
        notes=NET_ERR + NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_ioctl(s, cmd, argp);`. In `wsock32.dll` this function is ordinal 12 (in
  `ws2_32.dll` it is 10). `LONG` and `ULONG` are the documentation's `long` and `u_long`.
- Tests: `FIONBIO` with 1 and with 0 gives 0 and leaves last error alone; `FIONREAD` on a new
  socket stores 0; `argp` NULL gets `WSAEFAULT`; not a socket `WSAENOTSOCK`; the stop `core: any
  other command` (0x12345678).""",
    ),
    dict(
        id="net-sendto", lib="wsock32", members=["sendto"], needs=["rt-net-core"], ords={"sendto": 20},
        header="rt/net.h", core=["rt_net_sendto", "rt_net_recvfrom"], model="GetTickCount",
        protos=["int sendto(SOCKET s, LPCVOID buf, int len, int flags, const SOCKADDR *to, int tolen)"],
        notes=NET_ERR + NET_SETUP + NET_DATA + CORE_STOPS + NET_STOPS + """\
- `return rt_net_sendto(s, buf, len, flags, to, tolen);`.
- Tests: 5 bytes to 127.0.0.1 and the port of a second bound socket give 5, and
  `rt_net_recvfrom` on that socket gives the 5 bytes; to a loopback port nobody has: 5; to
  192.0.2.1: `SOCKET_ERROR`, `WSAENETUNREACH`, and `T_LOGGED(RT_CH_NET, "sendto 192.0.2.1")`; to
  0.0.0.0: `WSAEADDRNOTAVAIL`; 16 bytes from an unbound socket to 127.0.0.1 port 0: 16, port 0
  is not refused (Wine `ws2_32/tests/sock.c`, `test_UDP`; review D33); a send that succeeds
  leaves last error 0 (D22); 65508 bytes: `WSAEMSGSIZE`; a stream socket: `WSAENOTCONN`; not a
  socket: `WSAENOTSOCK`; the stop `core: flags other than 0`.""",
    ),
    dict(
        id="net-send", lib="wsock32", members=["send"], needs=["rt-net-core"], ords={"send": 19},
        header="rt/net.h", core=["rt_net_sendto", "rt_net_recvfrom"], model="GetTickCount",
        protos=["int send(SOCKET s, LPCVOID buf, int len, int flags)"],
        notes=NET_ERR + NET_SETUP + NET_DATA + CORE_STOPS + NET_STOPS + """\
- `return rt_net_sendto(s, buf, len, flags, NULL, 0);`.
- Tests: a datagram socket with no peer and a stream socket get `WSAENOTCONN`; after
  `rt_net_connect` to the bound port of a second socket, 3 bytes give 3 and arrive there; not a
  socket `WSAENOTSOCK`; the stop `core: flags other than 0`.""",
    ),
    dict(
        id="net-recvfrom", lib="wsock32", members=["recvfrom"], needs=["rt-net-core"], ords={"recvfrom": 17},
        header="rt/net.h", core=["rt_net_recvfrom", "rt_net_sendto"], model="GetTickCount",
        protos=["int recvfrom(SOCKET s, char *buf, int len, int flags, SOCKADDR *from, int *fromlen)"],
        notes=NET_ERR + NET_SETUP + NET_DATA + CORE_STOPS + NET_STOPS + """\
- `return rt_net_recvfrom(s, buf, len, flags, from, fromlen);`.
- Tests: THE ENGINE'S CASE FIRST: a bound, non-blocking socket with nothing queued gives
  `SOCKET_ERROR` and exactly `WSAEWOULDBLOCK`, with `from` and `*fromlen` untouched, ten times in
  a row; a queued datagram comes back with its bytes, the sender's address 127.0.0.1 and port
  in network order, `*fromlen` 16; a datagram longer than `len` fills the buffer and gives
  `WSAEMSGSIZE`, and the next call is `WSAEWOULDBLOCK` again; an unbound socket `WSAEINVAL`; not
  a socket `WSAENOTSOCK`; the stop `core: a blocking receive with nothing queued`.""",
    ),
    dict(
        id="net-recv", lib="wsock32", members=["recv"], needs=["rt-net-core"], ords={"recv": 16},
        header="rt/net.h", core=["rt_net_recvfrom", "rt_net_sendto"], model="GetTickCount",
        protos=["int recv(SOCKET s, char *buf, int len, int flags)"],
        notes=NET_ERR + NET_SETUP + NET_DATA + CORE_STOPS + NET_STOPS + """\
- `return rt_net_recvfrom(s, buf, len, flags, NULL, NULL);`.
- Tests: a stream socket gets `WSAENOTCONN`; a bound non-blocking datagram socket with nothing
  queued `WSAEWOULDBLOCK`; a queued datagram comes back; not a socket `WSAENOTSOCK`; the stop
  `core: flags other than 0` (`MSG_PEEK`).""",
    ),
    dict(
        id="net-shutdown", lib="wsock32", members=["shutdown"], needs=["rt-net-core"], ords={"shutdown": 22},
        header="rt/net.h", core=["rt_net_shutdown", "rt_net_sendto"], model="GetTickCount",
        protos=["int shutdown(SOCKET s, int how)"],
        notes=NET_ERR + NET_SETUP + """\
- `return rt_net_shutdown(s, how);`.
- Tests: a stream socket gets `WSAENOTCONN` (the game's only caller shuts down a TCP socket,
  engine+0x2dd964); `how` 3 gets `WSAEINVAL`; a datagram socket with `SD_SEND` gives 0 and a
  following `rt_net_sendto` gets `WSAESHUTDOWN`; not a socket `WSAENOTSOCK`.""",
    ),
    dict(
        id="net-select", lib="wsock32", members=["select"], needs=["rt-net-core"], ords={"select": 18},
        header="rt/net.h", core=["rt_net_select"], model="GetTickCount",
        protos=["int select(int nfds, FD_SET *readfds, FD_SET *writefds, FD_SET *exceptfds, const TIMEVAL *timeout)"],
        notes=NET_ERR + NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_select(nfds, readfds, writefds, exceptfds, timeout);`. `FD_SET` and `TIMEVAL`
  are the Windows structures of `rt/win/net.h` (the documentation's `fd_set` and `timeval`),
  filled by hand; the host's `FD_SET(n, p)` macro is never used.
- Tests (no test waits longer than 50 ms): three NULL sets and three empty sets get
  `WSAEINVAL`; a bound datagram socket in `readfds` with a timeout of {0, 0} gives 0 and an
  emptied set, and with {0, 20000} gives 0 after at least 20 ms; the same socket in `writefds`
  gives 1 and stays in the set; after a datagram is queued on it (`rt_net_sendto` from a
  second socket) `readfds` gives 1; a socket that is closed BEFORE the call gets `WSAENOTSOCK`
  and the sets are unchanged; (a socket closed by another thread DURING a
  wait is the core test's case, `tests/core/net_select.c`: a shim test starts no thread and may
  not include `pthread.h`); `nfds` is ignored (pass 0 and 1000); the
  `TIMEVAL` is unchanged afterwards.""",
    ),
    dict(
        id="net-gethostname", lib="wsock32", members=["gethostname"], needs=["rt-net-core"],
        ords={"gethostname": 57}, header="rt/net.h", core=["rt_net_gethostname"],
        model="GetTickCount",
        protos=["int gethostname(char *name, int namelen)"],
        notes=NET_ERR + NET_SETUP + """\
- `return rt_net_gethostname(name, namelen);`.
- Tests: a buffer of 0x200 bytes (the engine's, engine+0x219e79) gets 0 and the value of the
  environment variable `COMPUTERNAME` (read it with `rt_env_get_a`, `rt/env.h`), terminated,
  with last error left alone; a length one short of the name and its terminator, and a NULL
  buffer, get `WSAEFAULT`.""",
    ),
    dict(
        id="net-getaddrinfo", lib="ws2_32", members=["getaddrinfo"], needs=["rt-net-core"],
        header="rt/net.h", core=["rt_net_getaddrinfo", "rt_net_freeaddrinfo"],
        model="GetTickCount",
        protos=["INT getaddrinfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA *pHints, ADDRINFOA **ppResult)"],
        notes=NET_SETUP + CORE_STOPS + NET_STOPS + """\
- `return rt_net_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);`. The result is the
  error code, and the core also sets last error to it; the shim sets nothing.
- Tests (free every list with `rt_net_freeaddrinfo`): the engine's call (engine+0x41fa6c): a
  host name, no service, hints with `ai_family` `AF_INET6` and `ai_flags` `AI_V4MAPPED | AI_ALL`:
  for "example.invalid" the result is `WSAHOST_NOT_FOUND`, `*ppResult` is NULL, last error is
  the same code and `T_LOGGED(RT_CH_NET, "getaddrinfo example.invalid")`; for "localhost" two
  entries, each `ai_family` 23 and `ai_addrlen` 28, one with ::1 and one with ::ffff:127.0.0.1,
  in either order (review census-rows H2: loopback's IPv4 address comes too, mapped; the test
  does not assert the order); "127.0.0.1" with service "80" and
  NULL hints: one `AF_INET` entry with port 80 in network order, and last error, 0xDEAD
  before, is 0 after (review D22); node "" with service "0" succeeds, and "[::1]" with NULL
  hints gives one `AF_INET6` entry ::1 (Wine `ws2_32/tests/protocol.c`, `test_getaddrinfo`;
  review D35); both names NULL: `WSAHOST_NOT_FOUND`; `ai_family` 99: `WSAEAFNOSUPPORT`; the
  stop `core: a service that is not a number` ("http").""",
    ),
    dict(
        id="net-freeaddrinfo", lib="ws2_32", members=["freeaddrinfo"], needs=["rt-net-core"],
        header="rt/net.h", core=["rt_net_freeaddrinfo", "rt_net_getaddrinfo"],
        model="GetTickCount",
        protos=["void freeaddrinfo(ADDRINFOA *pAddrInfo)"],
        notes=NET_SETUP + """\
- `rt_net_freeaddrinfo(pAddrInfo);`. Status `full`.
- Tests: NULL does nothing and leaves last error alone (the engine frees the NULL of a failed
  lookup, engine+0x41fa7e), also before any `WSAStartup`; a list of `rt_net_getaddrinfo`
  ("localhost", no hints: two entries) is freed, after which the process heap has as many
  blocks as before (`rt_heap_validate` or the heap's own count, whichever `rt/heap.h` offers; if
  it offers neither, say so and check only that the call returns).""",
    ),
    # ==== end of the socket block ====
]

# The rows of user32, gdi32 and imm32 live in a file of their own (unit rt-contract-window), with
# three keys the rows above do not use: `waits` (units that will build a row's core piece and
# do not exist yet: the row is held while one has no file under swarm/units/, and the name
# counts as a need from the day it has), `model_lib` (the library directory of the model shim;
# kernel32 if absent) and `issue`.
import shim_rows_window                                           # noqa: E402
TASKS += shim_rows_window.WINDOW_TASKS


def pending(t):
    """The units a row waits for that have no unit file yet."""
    return [w for w in t.get("waits", []) if unit_state(w) is None]


BRIEF = """# Unit shim-{id}: {names}

This repository recompiles a 64-bit Windows game for arm64 macOS. When the game calls a
Windows function the call lands in a *shim*: a small C function that checks the arguments as
Windows documents, calls our runtime's core, and returns the result. {core_state} Write the shim and its test for: {names} (`{lib}.dll`).

## Files (create exactly these, edit nothing else)

{file_list}

## Read, in this order, and nothing else unless you get stuck

1. `runtime/README.md` (115 lines): the rules for a shim and its test. Follow it exactly.
2. `runtime/shims/{model}.c` and `runtime/tests/shims/{model}.c`: a finished
   shim and test to copy the shape from.
3. The Microsoft documentation page of each function, if you have a tool that fetches web
   pages (`learn.microsoft.com` only). Its parameters, return value and last-error behaviour
   are the specification, and the `SHIM` line copies its prototype with our type names. If you
   cannot fetch it, the notes below are enough: write `RT-DOC` as
   `https://learn.microsoft.com/windows/win32/api/<header>/nf-<header>-<function in lower case>`
   with the SDK header you believe declares the function, and say in your reply that the page
   was not read.
{shim_lines}{core_section}

Look in `runtime/include/rt/test.h` only for a test macro you need, and in
`runtime/include/rt/win/` only for a constant you need. If a type, constant or core function
you need does not exist, do not add it and do not work around it: stop and say exactly what
is missing.

## Rules

- **Never guess.** A case the documentation does not settle is `RT_UNIMPLEMENTED` (status
  `partial`, listed in `RT-TRAPS`) and goes in your reply. A decision the notes below state is
  not a guess of yours, also where it says "from memory" or "planner's choice": write it as
  given.
- An argument the shim never uses gets a `(void)name;` statement: the build has no warnings
  (item 4 of the check).
- The shim is thin: argument checks, one core call, the result. No `malloc`, no other shim.
- A test checks what the guest can see: return values, last error (also that success leaves
  it alone), every byte the function writes. Do not start threads in a shim test, unless
  the notes below say this one must.
- Every `T_CASE` text is an `RT-CASES` entry, spelled the same. A `T_TRAPS` line that proves an
  `RT-TRAPS` entry gets no `T_CASE` of its own: put it after the checks of the last case.

Notes for these functions:

{notes}

## Done

Each of these exits 0 on a clean, committed worktree. Run each as you finish that function and
commit when it passes (pipe the output through `tail -15`{family}):

{check_block}
"""

CORE_SECTION = """
## The core you call

From `runtime/include/{header}` (include it in the shim as `"{header}"`). These comments are the
specification of the core; do not re-implement any of it:

```c
{excerpt}
```"""

NO_CORE_SECTION = """
## No core function

These shims call no core function: in this runtime the answer is fixed, and the notes below
give it and where it comes from. Write exactly that answer; add no behaviour of your own."""

SHIM_LINES = """
## The `SHIM` line of each shim

Copy it as it is, on one line (the types are the ones `runtime/tools/types.def` knows; where a
type differs from the documentation's, the notes say why):

{lines}
"""


# The method bodies of the do-nothing Direct3D device (unit d3d-contract) are rows of another
# shape (two files per METHOD, checked by `make method-check`): tools/swarm/d3d_rows.py holds
# them with their own brief; this file only calls its problems(), show(), listing() and write().
import d3d_rows                                                    # noqa: E402
import shim_rows_r3                                                # noqa: E402
TASKS += shim_rows_r3.rows(sys.modules[__name__])
import shim_rows_census                                            # noqa: E402
TASKS += shim_rows_census.rows(sys.modules[__name__])   # unit rt-census-rows: what a recorded start calls
STAGES = "ABCDE"


class TableError(Exception):
    pass


def excerpt(header, names, decl_only=()):
    """The comment block and declaration of each core function, as the header has them; the
    declaration alone for a name in decl_only."""
    path = os.path.join(ROOT, "runtime", "include", header)
    if not os.path.exists(path):
        raise TableError("runtime/include/%s does not exist" % header)
    lines = open(path).read().split("\n")
    out = []
    for name in names:
        hits = [i for i, l in enumerate(lines) if re.search(r"\b%s\s*\(" % re.escape(name), l)
                and not l.lstrip().startswith(("*", "/*", "//"))]
        if not hits:
            raise TableError("runtime/include/%s does not declare %s" % (header, name))
        i = hits[0]
        a = i
        while name not in decl_only and a > 0 and lines[a - 1].strip() and \
                lines[a - 1].lstrip().startswith(("/*", "*", "//")):
            a -= 1
        b = i
        while ";" not in lines[b]:
            b += 1
        out.append("\n".join(lines[a:b + 1]))
    return "\n\n".join(out)


def excerpts(t):
    only = t.get("decl_only", ())
    out = excerpt(t["header"], t["core"], only) if t.get("header") else ""
    for header, names in t.get("more", []):
        if out:
            out += '\n\n/* ---- from runtime/include/%s (include it as "%s" too) ---- */\n\n' % (header, header)
        out += excerpt(header, names, only)
    return out


def parse_proto(proto):
    """'BOOL CloseHandle(HANDLE hObject)' -> ('BOOL', 'CloseHandle', ['HANDLE hObject'])."""
    m = re.fullmatch(r"\s*(.+?)\s*\b([A-Za-z_]\w*)\s*\((.*)\)\s*;?\s*", proto, re.S)
    if not m:
        raise TableError("cannot read the prototype '%s'" % proto)
    params = " ".join(m.group(3).split())
    return m.group(1), m.group(2), ([] if params in ("", "void") else shimdef._split_args(params))


def shim_line(lib, proto, ords=None):
    ret, name, params = parse_proto(proto)
    if ords and name in ords:                       # an import by ordinal (rows of the socket block)
        return "SHIM_ORD(%s)" % ", ".join([lib, str(ords[name]), ret, name] + params)
    return "SHIM(%s)" % ", ".join([lib, ret, name] + params)


def model_path(t):
    """'kernel32/Name' for a model given as 'Name', the row's own spelling for 'lib/Name'."""
    return t["model"] if "/" in t["model"] else t.get("model_lib", "kernel32") + "/" + t["model"]


def import_ordinals():
    """{(library id, function): ordinal} for the rows of imports.def that have one."""
    out = {}
    for raw in open(os.path.join(ROOT, "runtime", "tools", "imports.def")):
        f = raw.split("#", 1)[0].split()
        if len(f) >= 3 and f[2].isdigit():
            out[(shimdef.lib_id(f[0]), f[1])] = int(f[2])
    return out


def shim_path(lib, member):
    return os.path.join(ROOT, "runtime", "shims", lib, member + ".c")


def existing_proto(lib, member):
    """The prototype in a shim that is already written, or None."""
    path = shim_path(lib, member)
    if not os.path.exists(path):
        return None
    m = re.search(r"^SHIM(_ORD)?\((.*)\)\s*$", open(path).read(), re.M)
    if not m:
        return None
    args = shimdef._split_args(m.group(2))
    if m.group(1):
        args.pop(1)                                    # the ordinal
    return "%s %s(%s)" % (args[1], args[2], ", ".join(args[3:]))


def build(t):
    uid = "shim-" + t["id"]
    names = ["%s/%s" % (t["lib"], m) for m in t["members"]]
    cmds = []
    for n in names:
        others = ",".join(x for x in names if x != n)
        cmds.append("make shim-check SHIM=%s%s" % (n, " SHIM_FAMILY=" + others if others else ""))
    files = []
    for m in t["members"]:
        files += ["runtime/shims/%s/%s.c" % (t["lib"], m), "runtime/tests/shims/%s/%s.c" % (t["lib"], m)]
    text = excerpts(t)
    if t.get("header"):
        core_section = CORE_SECTION.format(header=t["header"], excerpt=text)
        core_state = "The core you need exists\nand is tested."
    elif text:
        header = t["more"][0][0]
        core_section = CORE_SECTION.format(header=header, excerpt=text)
        core_state = "The core you need exists\nand is tested."
    else:
        core_section = NO_CORE_SECTION
        core_state = "These functions need no core\nfunction."
    lines = "\n".join("    " + shim_line(t["lib"], p, t.get("ords")) for p in t.get("protos", []))
    brief = BRIEF.format(id=t["id"], lib=t["lib"], names=", ".join("`%s`" % m for m in t["members"]),
                         file_list="\n".join("- `%s`" % f for f in files), model=model_path(t),
                         core_state=core_state, core_section=core_section,
                         shim_lines=SHIM_LINES.format(lines=lines) if lines else "",
                         notes=(NET_ORD if t.get("ords") else "") + t["notes"], check_block="\n".join("    " + c for c in cmds),
                         family="; `SHIM_FAMILY` names the other function of this task, whose two files may "
                                "differ from the base as well" if len(names) > 1 else "")
    unit = {"id": uid, "title": "Shims: %s (%s)" % (", ".join(t["members"]), t["lib"]), "kind": "shim-task",
            "issue": t.get("issue", 24), "model": "haiku", "priority": 20 + STAGES.index(t.get("stage", "A")),
            "needs": t["needs"] + [w for w in t.get("waits", []) if w not in pending(t)], "check": cmds, "state": "todo"}
    return uid, brief, unit


def imported():
    """(library id, function) of every row of imports.def and imports_extra.def."""
    out = set()
    for name in ("imports.def", "imports_extra.def"):
        for raw in open(os.path.join(ROOT, "runtime", "tools", name)):
            f = raw.split("#", 1)[0].split()
            if len(f) >= 2:
                out.add((shimdef.lib_id(f[0]), f[1]))
    return out


def unit_state(uid):
    path = os.path.join(ROOT, "swarm", "units", uid + ".json")
    if not os.path.exists(path):
        return None
    return json.load(open(path)).get("state", "?")


def check():
    """Every reason the table cannot be turned into briefs a worker can finish."""
    problems = []
    types, _ = shimdef.load_types([os.path.join(ROOT, "runtime", "tools", "types.def")])
    known = imported()
    ordinals = import_ordinals()
    seen = {}
    ids = set()
    for t in TASKS:
        row = "row %s" % t["id"]
        if t["id"] in ids:
            problems.append("%s: the id is used twice" % row)
        ids.add(t["id"])
        if t.get("stage", "A") not in STAGES or len(t.get("stage", "A")) != 1:
            problems.append("%s: stage '%s' is not one of A to E" % (row, t.get("stage")))
        for name in t.get("decl_only", ()):
            if name not in t.get("core", []) and not any(name in names for _, names in t.get("more", [])):
                problems.append("%s: decl_only names %s, which is not one of the row's core functions" % (row, name))
        if not 1 <= len(t["members"]) <= 2:
            problems.append("%s: %d functions; a row is one or two" % (row, len(t["members"])))
        for header, names in ([(t["header"], t["core"])] if t.get("header") else []) + list(t.get("more", [])):
            for name in names:
                try:
                    excerpt(header, [name])
                except TableError as e:
                    problems.append("%s: %s" % (row, e))
        if not t.get("header") and t.get("core"):
            problems.append("%s: core functions without a header" % row)
        for kind in ("shims", "tests/shims"):
            rel = "runtime/%s/%s.c" % (kind, model_path(t))
            if not os.path.exists(os.path.join(ROOT, rel)):
                problems.append("%s: the model %s does not exist" % (row, rel))
        for need in t["needs"]:
            if unit_state(need) is None:
                problems.append("%s: needs '%s', and there is no swarm/units/%s.json" % (row, need, need))
        protos = {}
        for p in t.get("protos", []):
            try:
                protos[parse_proto(p)[1]] = p
            except TableError as e:
                problems.append("%s: %s" % (row, e))
        for name in protos:
            if name not in t["members"]:
                problems.append("%s: a prototype for %s, which is not one of the row's functions" % (row, name))
        for m in t["members"]:
            key = (t["lib"], m)
            if ordinals.get(key) != t.get("ords", {}).get(m):
                problems.append("%s: %s is imported %s, and the row's `ords` says %s" % (
                    row, m, "by ordinal %d" % ordinals[key] if key in ordinals else "by name",
                    t.get("ords", {}).get(m, "nothing")))
            if key in seen:
                problems.append("%s: %s is also in row %s" % (row, m, seen[key]))
            seen[key] = t["id"]
            if key not in known:
                problems.append("%s: %s!%s is not in runtime/tools/imports.def or imports_extra.def" % (
                    row, t["lib"], m))
            proto = protos.get(m) or existing_proto(t["lib"], m)
            if proto is None:
                problems.append("%s: no prototype for %s: add it to the row's protos" % (row, m))
                continue
            try:
                ret, _, params = parse_proto(proto)
                shimdef.parse_type(ret, types, "%s: %s returns" % (row, m), False)
                for p in params:
                    shimdef.parse_type(p, types, "%s: %s" % (row, m), True)
            except (TableError, shimdef.ShimError) as e:
                problems.append(str(e) if isinstance(e, shimdef.ShimError) else "%s: %s" % (row, e))
    return problems + ["window rows: " + p for p in shim_rows_window.problems()] + \
        ["d3d rows: " + p for p in d3d_rows.problems()]


def listing():
    for t in TASKS:
        if t.get("hold") or pending(t):
            print("%-24s %-58s HELD: %s" % ("shim-" + t["id"], " ".join("%s/%s" % (t["lib"], m) if t["lib"] != "kernel32"
                                                                        else m for m in t["members"]),
                                            t.get("hold") or "waits for " + ", ".join(pending(t))))
            continue
        marks = " ".join("%s[%s]" % (m, "written" if os.path.exists(shim_path(t["lib"], m)) else "-")
                         for m in t["members"])
        needs = ", ".join("%s (%s)" % (n, unit_state(n) or "NO UNIT") for n in t["needs"]) or "nothing"
        print("%-24s %-58s needs %s" % ("shim-" + t["id"], marks, needs))


def sizes():
    """Characters of every brief that is not written yet, largest first (a brief over about
    12,000 costs a worker more than it should: docs/handoff/rt-shim-rows-2.md)."""
    rows = []
    for t in TASKS:
        if all(os.path.exists(shim_path(t["lib"], m)) for m in t["members"]):
            continue
        rows.append((len(build(t)[1]), t))
    for n, t in sorted(rows, key=lambda r: -r[0]):
        print("%6d  %-26s %s%s" % (n, "shim-" + t["id"], " ".join(t["members"]), "  (held)" if t.get("hold") else ""))
    print("%d briefs not yet written as shims; %d over 12,000 characters" % (
        len(rows), sum(1 for n, _ in rows if n > 12000)))


def main():
    args = sys.argv[1:]
    if args == ["--list"]:
        listing()
        d3d_rows.listing()
        return
    if args == ["--sizes"]:
        sizes()
        return
    problems = check()
    if args == ["--check"]:
        for p in problems:
            print(p)
        print("%d rows, %d functions: %s" % (len(TASKS), sum(len(t["members"]) for t in TASKS),
                                             "%d problems" % len(problems) if problems else "ok"))
        print("d3d rows: " + d3d_rows.counts())
        sys.exit(1 if problems else 0)
    show = args[1] if len(args) == 2 and args[0] == "--show" else None
    if show and d3d_rows.show(show) is not None:
        print(d3d_rows.show(show))
        return
    if args and not show:
        sys.exit(__doc__)
    if problems and not show:
        sys.exit("nothing written, the table has problems:\n" + "\n".join(problems))
    found = False
    for t in TASKS:
        try:
            uid, brief, unit = build(t)
        except TableError as e:
            if show and show not in ("shim-" + t["id"], t["id"]):
                continue
            sys.exit("row %s: %s" % (t["id"], e))
        if show:
            if show in (uid, t["id"]):
                print(brief)
                found = True
            continue
        if t.get("hold") or pending(t):
            print("held  %s: %s" % (uid, t.get("hold") or "waits for " + ", ".join(pending(t))))
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
    if show and not found:
        sys.exit("no row %s" % show)
    if not show:
        d3d_rows.write()


if __name__ == "__main__":
    main()
