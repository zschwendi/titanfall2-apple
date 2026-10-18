"""Rows of tools/swarm/make_shim_tasks.py written in round 3 by unit rt-shim-rows-2.

Part 1: kernel32, in the order of the boot (stage A launcher, B file system, C engine, D and E:
tools/scout/boot_import_gap.py).  Part 2: the small libraries; every row there is a PROPOSAL and
is held ("orchestrator: ...") until the table in docs/handoff/rt-shim-rows-2.md has been read.
A row held with "core: ..." waits for a core piece that no ledger unit builds yet; the handoff
describes each piece.  Where a note says "from memory" the statement was not checked; "page"
means the function's page on learn.microsoft.com, read on 2026-10-10.

rows(N) gets the generator's module for its shared note blocks (FILE_ERR and the others).
"""

NULL_PTR = """\
- A NULL pointer where the page does not say what happens is `RT_UNIMPLEMENTED` with the
  function's name (planner's rule since round 2); list it in `RT-TRAPS`.
"""
ADVISORY = """\
- Status `advisory` (`docs/RUNTIME.md` section 9.2: Windows documents the call as a hint, and
  the runtime has nothing to adjust). The shim checks what the notes say, logs once with
  `rt_log_once(RT_CH_THREAD, RT_LL_DEBUG, "<Name>", "<Name>(...): accepted and ignored")` and
  succeeds. Last error is left alone on success. The test checks the log line with `T_LOGGED`.
"""
NO_CONSOLE = """\
- Status `absent`: this runtime has no console (`docs/RUNTIME.md` section 7.3: "Every console
  function reports that no console is attached"), so each call fails as Windows fails for a
  handle that is not a console's: FALSE with `ERROR_INVALID_HANDLE` (Wine's kernel32 console.c
  tests assert it for NULL, INVALID_HANDLE_VALUE and file handles). `RT-TRAPS` is `none`;
  no argument is looked at and nothing is written through any pointer.
- Log before failing: `rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!<Name>", "<Name>: there
  is no console");`. The test checks the line with `T_LOGGED(RT_CH_FILE, "<Name>: there is no
  console")`.
"""
TRAP = """\
- Status `trap`: `RT-CASES: none`, `RT-TRAPS: every call`. The whole body is one
  `RT_UNIMPLEMENTED` and nothing follows it (the macro does not return).
  `runtime/tests/abi/rttest/TrapOnly.c` is a shim of this status. The test is one `T_TRAPS`
  statement per function, with no `T_CASE`.
"""
A_TEXT = """\
- The `A` function converts in the shim, on the stack (no allocation): text comes in through
  `WCHAR wide[1024]` and `rt_mb_to_wide(CP_ACP, 0, src, n, wide, 1024)`; a result of 0 with last
  error `ERROR_INSUFFICIENT_BUFFER` (more than 1024 characters) is `RT_UNIMPLEMENTED`, any other
  0 is the function's failure with the code the core set. In code page 1252 one byte is one
  WCHAR, so counts are the same in both forms. Text goes out through
  `rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, n, narrow, size, NULL, &used)`; `used` TRUE
  (a character outside code page 1252) is `RT_UNIMPLEMENTED`.
"""
HELD_CORE = """\
- THIS ROW IS HELD: the core function it needs does not exist yet. The piece is described in
  `docs/handoff/rt-shim-rows-2.md` ("Core pieces to build"). When it is built, add its header
  and function to this row (`header=`, `core=`), name its unit in `needs` and delete `hold`.
"""
PROPOSAL = """\
- What this row answers was proposed in the table of `docs/handoff/rt-shim-rows-2.md` ("The
  small libraries") and released by the orchestrator on 2026-10-10: it is a decision.
"""
NOTHING_THERE = """\
- Status `absent` (`docs/RUNTIME.md` section 9.2): the machine this runtime describes does not
  have the device or service, and the shim returns the failure Windows documents for such a
  machine. Nothing is written through any pointer unless a note says so. Log before failing:
  `rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "<lib>.dll!<Name>", "<Name>: <what is not there>");` and
  check the line in the test with `T_LOGGED(RT_CH_IMPORT, ...)`. `RT-TRAPS` is `none` unless a
  note names a stop. The planner's sign-off that the README asks for is this row.
"""


def rows(N):
    FE, FT, FN, CS = N.FILE_ERR, N.FILE_TEST, N.FILE_NAME, N.CORE_STOPS
    out = []

    def row(**kw):
        out.append(kw)

    # ================= part 1: kernel32 ==========================================================
    # ---- stage A: the launcher -------------------------------------------------------------------
    row(id="env-get-a", lib="kernel32", members=["GetEnvironmentVariableA"], needs=["rt-core-env"], stage="A",
        header="rt/env.h", core=["rt_env_get_w", "rt_env_get_a", "rt_env_set_a"], model="SetEnvironmentVariableA",
        protos=["DWORD GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)"],
        notes=CS + """\
- The comment of `rt_env_get_w` is the specification; the A form differs as its own comment
  says. The body: `DWORD err = 0; DWORD n = rt_env_get_a(lpName, lpBuffer, nSize, &err); if (err)
  return FAIL(ctx, err, 0); if (n == 0) rt_set_last_error(ctx, ERROR_SUCCESS); return n;`.
- Tests (set the variables with `rt_env_set_a`): a value is copied and its length returned,
  last error left alone; the name in another case finds it; the size query (NULL, 0) and a
  buffer one byte too small return the length plus one and leave a preset buffer alone, last
  error left alone; a variable that does not exist (0, `ERROR_ENVVAR_NOT_FOUND`, buffer
  untouched); a variable set to the empty string (0 and last error `ERROR_SUCCESS`, after a
  preset of 0x1234); the stop `core: a NULL name`.""")
    row(id="file-attr-w", lib="kernel32", members=["GetFileAttributesExW", "SetFileAttributesW"],
        needs=["core-file-attr-body"], stage="A", header="rt/file.h",
        core=["rt_file_get_attributes_ex", "rt_file_set_attributes", "rt_file_get_attributes"],
        model="GetFileAttributesExA",
        protos=["BOOL GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, "
                "LPVOID lpFileInformation)",
                "BOOL SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes)"],
        notes=FE + FN + FT + CS + """\
- One core call each: `rt_file_get_attributes_ex(RT_NAME_W(lpFileName), (DWORD)fInfoLevelId,
  lpFileInformation)` and `rt_file_set_attributes(RT_NAME_W(lpFileName), dwFileAttributes)`.
  These are the W twins of `GetFileAttributesExA` and `SetFileAttributesA`, which are written:
  the same tests, with the names as `W("...")`.
- Tests for the getter: a file of 5 bytes (attributes as `rt_file_get_attributes` gives them,
  size 5); a directory; a level of 1 (`ERROR_INVALID_PARAMETER`); a missing file
  (`ERROR_FILE_NOT_FOUND`), the 36 bytes untouched in both failures; success leaves last error
  alone; the stop `core: lpFileInformation NULL`.
- Tests for the setter: `FILE_ATTRIBUTE_READONLY` and then `FILE_ATTRIBUTE_NORMAL` on a file,
  seen through `rt_file_get_attributes` and the host mode (`stat` on `I(...)`); a missing file;
  a NULL name fails with the code `rt_file_get_attributes(RT_NAME_W(NULL))` gives; the stop
  `core: an attribute bit that cannot be set` (`FILE_ATTRIBUTE_DIRECTORY`).""")
    row(id="file-pointer", lib="kernel32", members=["SetFilePointer"], needs=["core-file-io-body"], stage="A",
        header="rt/file.h", core=["rt_file_seek", "rt_file_set_eof"], model="SetFilePointerEx",
        protos=["DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, "
                "DWORD dwMoveMethod)"],
        notes=FE + FT + CS + """\
- The page settles this, and the file contract wrote the same rule down
  (`docs/handoff/rt-contract-file.md`, "Shims this unlocks"; `rt/file.h` marks the function
  `(*)` for it). 50 call sites on the boot path, so follow it to the letter:
  1. The distance is `(LONGLONG)lDistanceToMove` with a NULL high pointer (the page: "a 32-bit
     signed value", so a negative distance from `FILE_BEGIN` is a position before the start and
     the core refuses it), else the 64-bit value with `*lpDistanceToMoveHigh` as its high half
     and `(DWORD)lDistanceToMove` as its low half (`(LONGLONG)*lpDistanceToMoveHigh * 4294967296
     + (DWORD)lDistanceToMove`: do not shift a negative number).
  2. `LONGLONG pos; if (!rt_file_seek(hFile, dist, &pos, dwMoveMethod)) return
     INVALID_SET_FILE_POINTER;` (the core set last error, `ERROR_NEGATIVE_SEEK` included, and
     moved nothing; `*lpDistanceToMoveHigh` is not written).
  3. With a NULL high pointer and `pos > 0xFFFFFFFF`: the page says the function fails ("the new
     file position does not fit in a 32-bit value") and names no code, and the core has moved the
     position already: `RT_UNIMPLEMENTED(ctx, "SetFilePointer with NULL lpDistanceToMoveHigh and a
     position of %lld", (long long)pos)`. Status `partial`; the `RT-TRAPS` entry is `SetFilePointer
     with NULL lpDistanceToMoveHigh` (`T_TRAPS` looks for its text inside the report).
  4. With a high pointer, store `(LONG)(pos >> 32)` in it. Return `(DWORD)pos`; if that value is
     `INVALID_SET_FILE_POINTER` (0xFFFFFFFF), first `rt_set_last_error(ctx, ERROR_SUCCESS)`, with
     or without a high pointer (the page: the caller tells success by `GetLastError() ==
     NO_ERROR`; for the NULL case that is Wine's code as remembered). Otherwise last error is
     left alone.
- Tests (a file of 10 bytes; `rt_file_seek(h, 0, &pos, FILE_CURRENT)` is the oracle for the
  position): `FILE_BEGIN` 4, `FILE_CURRENT` -1 and `FILE_END` 0 with a NULL high pointer, last
  error left alone; a high pointer holding 1 and a low distance of 0x10 from the start returns
  0x10 and leaves 1 in it (a position beyond the end is allowed); back to 3 with a high pointer
  holding 0; a move before the start (`INVALID_SET_FILE_POINTER`, `ERROR_NEGATIVE_SEEK`, the
  position and the high value unchanged); position 0xFFFFFFFF from the start with a high
  pointer holding 0 (returns 0xFFFFFFFF with last error 0 after a preset of 0x1234); a method of
  3 (`ERROR_INVALID_PARAMETER`); a handle that is not open (`ERROR_INVALID_HANDLE`). The stop of
  step 3: go to 0x100000000 with a high pointer, then `FILE_CURRENT` 0 with NULL.""")
    row(id="console-screen", lib="kernel32", members=["GetConsoleScreenBufferInfo", "SetConsoleTextAttribute"],
        needs=["rt-contract-file"], stage="A", header=None, core=[], model="GetConsoleMode",
        protos=["BOOL GetConsoleScreenBufferInfo(HANDLE hConsoleOutput, "
                "PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo)",
                "BOOL SetConsoleTextAttribute(HANDLE hConsoleOutput, WORD wAttributes)"],
        notes=NO_CONSOLE + """\
- Both: `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);` (signed off in
  `docs/handoff/rt-contract-file.md`). `CONSOLE_SCREEN_BUFFER_INFO` (22 bytes) is in
  `rt/win/file.h`.
- Tests: each with `(HANDLE)0x44`, with the handle `rt_std_get(STD_OUTPUT_HANDLE)` (`rt/file.h`)
  and with NULL; a structure preset to 0xA5 keeps its 22 bytes; the last error; the log line.""")
    row(id="create-process", lib="kernel32", members=["CreateProcessA", "CreateProcessW"],
        needs=["rt-contract-file"], stage="A", header=None, core=[], model="SuspendThread",
        protos=["BOOL CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, "
                "LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, "
                "BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, "
                "LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)",
                "BOOL CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, "
                "LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, "
                "BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, "
                "LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)"],
        notes=TRAP + """\
- Child processes are a feature this machine does not have, "decided when a caller appears"
  (`docs/RUNTIME.md` section 7.3). The stop must say what was to be started, which is what the
  owner needs to decide. `CreateProcessA`: `RT_UNIMPLEMENTED(ctx, "CreateProcessA(application
  \\"%s\\", command line \\"%s\\", flags 0x%X)", lpApplicationName ? lpApplicationName : "(null)",
  lpCommandLine ? lpCommandLine : "(null)", (unsigned)dwCreationFlags);`.
- `CreateProcessW`: the strings are UTF-16 and the report prints bytes, so print the two
  pointers and the flags: `RT_UNIMPLEMENTED(ctx, "CreateProcessW(application %p, command line %p,
  flags 0x%X); run with RT_LOG=trace to see the strings", ...)`.
- Tests: `T_TRAPS(CALL(kernel32, CreateProcessA, NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si,
  &pi), "CreateProcessA(application \\"(null)\\", command line \\"notepad.exe x\\"")` with `char cmd[] =
  "notepad.exe x"`; `CreateProcessW` with every pointer NULL, expecting `"CreateProcessW(application"`.""")
    row(id="device-io", lib="kernel32", members=["DeviceIoControl"], needs=["rt-contract-file"], stage="A",
        header=None, core=[], model="SuspendThread",
        protos=["BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, "
                "LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)"],
        notes=TRAP + """\
- Nobody knows which device and control code tier0's nine call sites use (`docs/RUNTIME.md`
  11.3). The stop says it: `RT_UNIMPLEMENTED(ctx, "DeviceIoControl(handle %p, code 0x%08X, %u
  bytes in, %u bytes out)", hDevice, (unsigned)dwIoControlCode, (unsigned)nInBufferSize,
  (unsigned)nOutBufferSize);`.
- Test: `T_TRAPS(CALL(kernel32, DeviceIoControl, (HANDLE)(uintptr_t)0x44, 0x2D1400, NULL, 0, NULL,
  0, NULL, NULL), "DeviceIoControl(handle 0x44, code 0x002D1400, 0 bytes in, 0 bytes out)");`.""")
    row(id="exec-state", lib="kernel32", members=["SetThreadExecutionState"],
        needs=["rt-contract-thread", "rt-core-small"], stage="A", header="rt/thread.h",
        core=["rt_thread_execution_state"], model="SetPriorityClass",
        protos=["EXECUTION_STATE SetThreadExecutionState(EXECUTION_STATE esFlags)"],
        notes=CS + """\
- The launcher calls this (2 sites) to keep the display awake. One core call: `return
  rt_thread_execution_state(ctx, esFlags);`. The core keeps the state per thread, sets last
  error itself when it fails and tells the host nothing. Status `partial`: the core stops on
  what the page leaves open, and those stops are the `RT-TRAPS` entries (`core: ...`).
- Tests, with the values of the core's comment: a first call with `ES_CONTINUOUS |
  ES_DISPLAY_REQUIRED` returns `ES_CONTINUOUS`; a second call returns the flags of the first; a
  call without `ES_CONTINUOUS` returns the state and leaves it as it is (the next call shows
  that); `ES_USER_PRESENT | ES_CONTINUOUS` returns 0 with `ERROR_INVALID_PARAMETER` after a
  preset of 0x1234; success leaves a preset last error alone. Constants: `rt/win/process.h`.""")

    # ---- stage B: rtech_game and filesystem_stdio ------------------------------------------------
    row(id="read-file-ex", lib="kernel32", members=["ReadFileEx"], needs=["rt-file-ov"], stage="B",
        header="rt/file.h", core=["rt_file_read_ex"], model="ReadFile",
        protos=["BOOL ReadFileEx(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, "
                "LPOVERLAPPED lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)"],
        notes=FE + FT + CS + """\
- One core call: `return rt_file_read_ex(ctx, hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped,
  lpCompletionRoutine);`. The core sets last error on success too (0), as its comment says: the
  shim sets nothing. This is the game's main data path (38,354 calls in the traced run).
- The test needs a file opened with `FILE_FLAG_OVERLAPPED` (`rt_file_create`), a completion
  routine written with `GUEST_FN` (`rt/test.h`: it reads its three arguments from `ctx->r[1]`,
  `ctx->r[2]` and `ctx->r[8]` and stores them in statics) and an alertable sleep to run it:
  `rt_sleep_ex(T_CTX(), 0, TRUE)` (`rt/sync.h`) returns `WAIT_IO_COMPLETION` when it ran one.
  `runtime/tests/core/file_ov.c` tests the core function this way: search it for
  `rt_file_read_ex` and read only those tests.
- Tests: a read of 4 bytes at offset 2 returns TRUE with last error 0 (preset 0x1234), the
  bytes are in the buffer, the routine has not run before the sleep and has run after it with
  (0, 4, the OVERLAPPED's address); a read at the end of the file returns TRUE and the routine
  gets (`ERROR_HANDLE_EOF`, 0, ov); a handle that is not open (`ERROR_INVALID_HANDLE`, nothing
  queued: the sleep returns 0); the stop `core: a handle without FILE_FLAG_OVERLAPPED`.""")
    row(id="ov-result", lib="kernel32", members=["GetOverlappedResult"], needs=["rt-file-ov"], stage="B",
        header="rt/file.h", core=["rt_file_overlapped_result", "rt_file_read"], model="ReadFile",
        protos=["BOOL GetOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, "
                "LPDWORD lpNumberOfBytesTransferred, BOOL bWait)"],
        notes=FE + FT + CS + """\
- One core call: `return rt_file_overlapped_result(hFile, lpOverlapped,
  lpNumberOfBytesTransferred, bWait);`.
- Tests (a file of 10 bytes; make each result with `rt_file_read(h, buf, len, NULL, &ov)` on a
  zeroed `OVERLAPPED` with `Offset` set): after a read of 4 bytes: TRUE and 4, with `bWait`
  FALSE and TRUE, last error left alone; after a read at offset 10: FALSE, 0 bytes,
  `ERROR_HANDLE_EOF`; an `OVERLAPPED` whose `Internal` the test set to `STATUS_PENDING`
  (`rt/win/file.h`) with `bWait` FALSE: `ERROR_IO_INCOMPLETE`; a handle that is not open
  (`ERROR_INVALID_HANDLE`); the stops `core: STATUS_PENDING with bWait TRUE` and `core:
  lpOverlapped NULL`.""")
    row(id="file-size", lib="kernel32", members=["GetFileSizeEx", "GetFileSize"], needs=["core-file-io-body"],
        stage="B", header="rt/file.h", core=["rt_file_size", "rt_file_seek", "rt_file_set_eof"], model="SetFilePointerEx",
        protos=["BOOL GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)",
                "DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)"],
        notes=FE + FT + CS + """\
- `GetFileSizeEx`: `return rt_file_size(hFile, lpFileSize ? &lpFileSize->QuadPart : NULL);` (the
  core stops on NULL: list `core: lpFileSize NULL`).
- `GetFileSize`, from its page: `LONGLONG size; if (!rt_file_size(hFile, &size)) return
  INVALID_FILE_SIZE;` (the core set last error). Store `(DWORD)(size >> 32)` through
  `lpFileSizeHigh` if it is not NULL and return `(DWORD)size`. If that value is
  `INVALID_FILE_SIZE` (0xFFFFFFFF), first `rt_set_last_error(ctx, ERROR_SUCCESS)` ("GetLastError
  will return NO_ERROR (0) upon success"). With a NULL high pointer and a size above
  0xFFFFFFFF: `RT_UNIMPLEMENTED` with the size (the file contract's rule,
  `docs/handoff/rt-contract-file.md`: the page calls the result "ambiguous", and a caller that
  gets a cut size should be seen). Status `partial`.
- Tests: a file of 10 bytes (both functions; the high value becomes 0); an empty file; a
  handle that is not open (`ERROR_INVALID_HANDLE`, and `INVALID_FILE_SIZE` from `GetFileSize`
  with the high value untouched); success leaves last error alone. Large sizes without writing
  data: set the position with `rt_file_seek` and call `rt_file_set_eof` (the file is sparse):
  0x100000005 bytes gives low 5 and high 1, and is the stop with a NULL high pointer;
  0xFFFFFFFF bytes gives 0xFFFFFFFF with high 0 and last error 0 after a preset of 0x1234, and
  the same with a NULL high pointer. Delete the file at the end. The stop `core: a character device` (the handle `rt_std_get(STD_OUTPUT_HANDLE)`).""")
    row(id="file-info-handle", lib="kernel32", members=["GetFileInformationByHandle"],
        needs=["core-file-attr-body"], stage="B", header="rt/file.h",
        core=["rt_file_info_by_handle", "rt_file_get_attributes"], model="GetFileAttributesExA",
        protos=["BOOL GetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation)"],
        notes=FE + FT + CS + """\
- One core call. `BY_HANDLE_FILE_INFORMATION` (52 bytes) is in `rt/win/file.h`.
- Tests: a file of 5 bytes: attributes as `rt_file_get_attributes` gives them, `nFileSizeLow`
  5, `nFileSizeHigh` 0, `dwVolumeSerialNumber` `RT_VOLUME_SERIAL`, `nNumberOfLinks` 1, a
  last-write time that is not 0, and a file index equal to the host inode (`stat` on `I(...)`);
  a directory opened with `FILE_FLAG_BACKUP_SEMANTICS` (`FILE_ATTRIBUTE_DIRECTORY`, size 0); a
  handle that is not open (`ERROR_INVALID_HANDLE`, the 52 bytes preset to 0xA5 untouched);
  success leaves last error alone; the stop `core: lpFileInformation NULL`.""")
    row(id="filetime-convert", lib="kernel32", members=["FileTimeToSystemTime", "FileTimeToLocalFileTime"],
        needs=["rt-core-time", "rt-core-locale"], stage="B", header="rt/locale.h",
        core=["rt_systemtime_from_filetime"], more=[("rt/time.h", ["rt_time_zone_bias_minutes",
                                                                  "rt_time_filetime_from_unix"])],
        model="GetSystemTimeAsFileTime",
        protos=["BOOL FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime)",
                "BOOL FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)"],
        notes=NULL_PTR + """\
- A `FILETIME` is two DWORDs: the value is `(uint64_t)dwHighDateTime << 32 | dwLowDateTime`.
  `RtSystemTime` is `SYSTEMTIME`. 103 call sites on the boot path between them.
- `FileTimeToSystemTime`: a value of 0x8000000000000000 or more: `return FAIL(ctx,
  ERROR_INVALID_PARAMETER, FALSE);` (the page: "This value must be less than
  0x8000000000000000. Otherwise, the function fails"; the code: Wine time.c
  test_FileTimeToSystemTime). Otherwise
  `rt_systemtime_from_filetime(value, lpSystemTime); return TRUE;`.
- `FileTimeToLocalFileTime`: the page says it "uses the current settings for the time zone",
  which here is one number: `local = value - (int64_t)rt_time_zone_bias_minutes() * 600000000`.
  A value of 0x8000000000000000 or more, or a result that is negative or that large, fails the
  same way (from memory). Store the two halves. The two pointers may be the same.
- Tests: `rt_time_filetime_from_unix(946684800, 0)` is 2000-01-01 00:00:00 UTC, a Saturday
  (`wDayOfWeek` 6): check every member; the same plus 1,234,567 units gives `wMilliseconds`
  123; value 0 is 1601-01-01, a Monday; 0x8000000000000000 fails with the output untouched;
  the local time of the 2000 value differs from it by exactly the bias, in the direction UTC =
  local + bias; a value smaller than a positive bias fails (only if the bias is positive: test
  what the host's zone allows and say which); success leaves last error alone; each NULL stop.""")
    row(id="local-time", lib="kernel32", members=["GetLocalTime", "GetSystemTime"],
        needs=["rt-core-time", "rt-core-locale"], stage="B", header="rt/locale.h",
        core=["rt_systemtime_from_filetime"],
        more=[("rt/time.h", ["rt_time_utc_100ns", "rt_time_set_source", "rt_time_zone_bias_minutes",
                             "rt_time_filetime_from_unix"])],
        decl_only=["rt_time_set_source"], model="GetSystemTimeAsFileTime",
        protos=["void GetSystemTime(LPSYSTEMTIME lpSystemTime)", "void GetLocalTime(LPSYSTEMTIME lpSystemTime)"],
        notes=NULL_PTR + """\
- `GetSystemTime`: `rt_systemtime_from_filetime(rt_time_utc_100ns(), lpSystemTime);`.
  `GetLocalTime`: the same on `rt_time_utc_100ns() - (int64_t)rt_time_zone_bias_minutes() *
  600000000` (UTC = local time + bias). Neither fails or touches last error.
- Tests: install a fixed wall clock with `rt_time_set_source(NULL, fixed)` where `fixed` returns
  `rt_time_filetime_from_unix(946684800, 0) + 1234567` (2000-01-01 00:00:00.123 UTC, a Saturday)
  and restore it with `rt_time_set_source(NULL, NULL)` at the end. `GetSystemTime` gives exactly
  those members (`wDayOfWeek` 6, `wMilliseconds` 123); `GetLocalTime` gives the members
  `rt_systemtime_from_filetime` computes for the shifted value; last error is left alone; each
  NULL stop.""")
    row(id="find-first-ex", lib="kernel32", members=["FindFirstFileExW", "FindFirstFileExA"],
        needs=["core-file-find-body"], stage="B", header="rt/file.h",
        core=["rt_find_first_w", "rt_find_first_a", "rt_find_next_w", "rt_find_close"],
        decl_only=["rt_find_next_w", "rt_find_close"], model="FindFirstFileA",
        protos=["HANDLE FindFirstFileExW(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, "
                "FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags)",
                "HANDLE FindFirstFileExA(LPCSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, "
                "FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags)"],
        notes=FE + FT + CS + """\
- The extra arguments, decided from the pages of the function and of its two enumerations
  (constants in `rt/win/file.h`), checked in this order, each stop naming the value:
  `fInfoLevelId` `FindExInfoStandard` or `FindExInfoBasic` (which only leaves out the short
  name, and the core never gives one), anything else `RT_UNIMPLEMENTED`; `fSearchOp`
  `FindExSearchNameMatch`, or `FindExSearchLimitToDirectories`, which is accepted and ignored
  ("This is an advisory flag. If the file system does not support directory filtering, this
  flag is silently ignored"), anything else `RT_UNIMPLEMENTED`; `lpSearchFilter` not NULL
  `RT_UNIMPLEMENTED` ("must be NULL"); `dwAdditionalFlags`: `FIND_FIRST_EX_LARGE_FETCH` is
  accepted and ignored (a buffer-size hint), any other bit `RT_UNIMPLEMENTED`
  (`FIND_FIRST_EX_CASE_SENSITIVE` too: the core matches without case).
- Then one core call: `rt_find_first_w(lpFileName, lpFindFileData)` or `rt_find_first_a`.
  Status `partial`.
- Tests, for both forms (a directory under the install root with two files and a
  sub-directory): the pattern `C:\\Titanfall2\\d\\*` gives `.` first, with both info levels,
  with `FindExSearchLimitToDirectories` (files are still returned: continue with
  `rt_find_next_w` until a file's name is seen) and with `FIND_FIRST_EX_LARGE_FETCH`; a pattern
  that matches nothing (`INVALID_HANDLE_VALUE`, `ERROR_FILE_NOT_FOUND`); success leaves last
  error alone; one `T_TRAPS` per stop (info level 2, search op 2, a filter, flag 1). Close
  every handle with `rt_find_close`.""")
    row(id="find-file-w", lib="kernel32", members=["FindFirstFileW", "FindNextFileW"],
        needs=["core-file-find-body"], stage="C", header="rt/file.h",
        core=["rt_find_first_w", "rt_find_next_w", "rt_find_close"], decl_only=["rt_find_close"],
        model="FindFirstFileA",
        protos=["HANDLE FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)",
                "BOOL FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)"],
        notes=FE + FT + CS + """\
- One core call each. These are the W twins of `FindFirstFileA` and `FindNextFileA`, which are
  written: the same tests with `W("...")` names and `WIN32_FIND_DATAW`.
- Tests: a directory under the install root with two files and a sub-directory; `*` gives `.`,
  `..` and the three names in the core's order, each with attributes and size, then FALSE with
  `ERROR_NO_MORE_FILES`, twice; `*.txt` gives only the matching file; no match
  (`INVALID_HANDLE_VALUE`, `ERROR_FILE_NOT_FOUND`); a directory that does not exist
  (`ERROR_PATH_NOT_FOUND`); `FindNextFileW` on NULL and on a file handle
  (`ERROR_INVALID_HANDLE`); success leaves last error alone; the stop `core: lpFindFileData
  NULL` for each function. Close every find handle with `rt_find_close`.""")
    row(id="full-path-w", lib="kernel32", members=["GetFullPathNameW"], needs=["rt-core-path"], stage="B",
        header="rt/path.h", core=["rt_path_full_w", "rt_path_get_cwd_w"], decl_only=["rt_path_full_w"],
        model="GetFullPathNameA",
        protos=["DWORD GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, "
                "LPWSTR *lpFilePart)"],
        notes=N.PATH_ERR + """\
- The core's comment is long; what the shim needs of it: `rt_path_full_w(win, out, cch, &len,
  &part)` returns a Win32 error code (0: success; a NULL or empty name is `ERROR_INVALID_NAME`)
  and stores in `len` what `GetFullPathNameW` returns: the length without the terminator when
  the result fits in `cch` WCHARs, else the size needed with the terminator (so it fits exactly
  when `len < cch`). `part` receives the index in `out` of the last component, or
  `RT_PATH_NO_PART`. `out` may be NULL only when `cch` is 0. The written `GetFullPathNameA` uses
  the same call.
- The steps: (1) `lpBuffer` NULL with `nBufferLength` not 0 is `RT_UNIMPLEMENTED`. (2) `e =
  rt_path_full_w(lpFileName, lpBuffer, nBufferLength, &len, &part); if (e) return FAIL(ctx, e,
  0);`. (3) If `len < nBufferLength` and `lpFilePart` is not NULL, store `part ==
  RT_PATH_NO_PART ? NULL : lpBuffer + part`; when the result did not fit, `*lpFilePart` is not
  written (planner's choice, as in the A form). (4) Return `len`. Status `partial`.
- Tests: `x.txt` gives the current directory (`rt_path_get_cwd_w`), a backslash and the name,
  the part pointing at `x.txt`; `C:\\a\\..\\b\\c.bin` gives `C:\\b\\c.bin`; `C:\\a\\` has a
  NULL part; `lpFilePart` NULL; the size query (0, NULL) and a buffer one WCHAR too small
  return the length plus one and leave a preset part alone; NULL and `L""` return 0 with
  `ERROR_INVALID_NAME`; success leaves last error alone; the stop of step 1.""")
    row(id="dir-create-w", lib="kernel32", members=["CreateDirectoryW"], needs=["core-file-dir-body"], stage="B",
        header="rt/file.h", core=["rt_dir_create"], model="CreateDirectoryA",
        protos=["BOOL CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)"],
        notes=FE + FN + FT + N.SEC + """\
- One core call. The game does this 116 times on the way to the menu, mostly on directories
  that exist.
- Tests: a directory is created (the host sees it: `stat` on `I(...)`); a second create and a
  create over a file fail with `ERROR_ALREADY_EXISTS`; a missing parent and a NULL name
  (`ERROR_PATH_NOT_FOUND`); success leaves last error alone.""")
    row(id="move-file-ex", lib="kernel32", members=["MoveFileExW"], needs=["core-file-dir-body"], stage="B",
        header="rt/file.h", core=["rt_file_move"], model="MoveFileA",
        protos=["BOOL MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags)"],
        notes=FE + FN + FT + CS + """\
- One core call: `rt_file_move(RT_NAME_W(lpExistingFileName), RT_NAME_W(lpNewFileName), dwFlags)`.
  The `MOVEFILE_` flags are in `rt/win/file.h`.
- Tests: a file moves to a new name (the host sees it there only); a target that exists fails
  with `ERROR_ALREADY_EXISTS` without `MOVEFILE_REPLACE_EXISTING` and is replaced with it (the
  target then has the source's bytes); a source that does not exist (`ERROR_FILE_NOT_FOUND`);
  a target directory that does not exist (`ERROR_PATH_NOT_FOUND`); `MOVEFILE_WRITE_THROUGH` is
  accepted; success leaves last error alone; the stops `core: lpNewFileName NULL` and `core:
  MOVEFILE_DELAY_UNTIL_REBOOT`.""")
    row(id="sem-create-ex", lib="kernel32", members=["CreateSemaphoreExW"], needs=["rt-core-wait"], stage="B",
        header="rt/sync.h", core=["rt_semaphore_create", "rt_semaphore_release"], model="CreateSemaphoreW",
        protos=["HANDLE CreateSemaphoreExW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, "
                "LONG lMaximumCount, LPCWSTR lpName, DWORD dwFlags, DWORD dwDesiredAccess)"],
        notes=N.SEC + N.SYNC_ERR + CS + """\
- `dwFlags` not 0 is `RT_UNIMPLEMENTED` with the value (the page: "This parameter is reserved
  and must be 0"). `dwDesiredAccess` is ignored: a handle of this runtime has no access rights
  of its own (say so in a comment). Then `return rt_semaphore_create(ctx, lInitialCount,
  lMaximumCount, lpName);`. Status `partial`.
- Tests, as the test of the written `CreateSemaphoreW` with the two extra arguments 0 and
  0x1F0003: no name (a handle, last error 0); a name twice (the second gives a handle and
  `ERROR_ALREADY_EXISTS`); an initial count above the maximum fails as the core's comment says;
  the count works (`rt_semaphore_release` returns the previous count); the stop for flags 1.
  Close every handle with `rt_handle_close`.""")
    row(id="debug-break", lib="kernel32", members=["DebugBreak"], needs=["core-crash-exception-body"], stage="B",
        header="rt/crash.h", core=["rt_debug_break"], model="UnhandledExceptionFilter",
        protos=["void DebugBreak(void)"],
        notes="""\
- One core call, which does not return: `rt_debug_break(ctx);`. Status `full`, `RT-TRAPS: none`:
  the report is the specified behaviour (`docs/RUNTIME.md` 9.4: no debugger is ever attached).
- Test, one statement and no `T_CASE` needed beyond the one `RT-CASES` entry it proves:
  `T_TRAPS(CALL(kernel32, DebugBreak), "FATAL guest called DebugBreak");`.""")
    row(id="disk-free", lib="kernel32", members=["GetDiskFreeSpaceA", "GetDiskFreeSpaceExA"],
        needs=["rt-contract-file", "rt-core-small"], stage="B", header="rt/file.h", core=["rt_disk_space"],
        model="GetDriveTypeW",
        protos=["BOOL GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, "
                "LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)",
                "BOOL GetDiskFreeSpaceExA(LPCSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailableToCaller, "
                "PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes)"],
        notes=FE + FT + CS + """\
- "Disk queries answer for the mapped root; `C:\\` is the only drive" (`docs/RUNTIME.md` 7.3).
  The name goes to the core as it came: `RT_NAME_A(lpRootPathName)` (a NULL name is the root of
  the drive, which the core handles). The core stops on a name that leads to a file: `RT-TRAPS`
  `core: a name that is not a directory`, status `partial`.
- `GetDiskFreeSpaceExA`: one core call, `return rt_disk_space(RT_NAME_A(lpDirectoryName), a, t,
  f);`, where each of `a`, `t`, `f` is NULL for a NULL pointer of the guest and else the address
  of that structure's `QuadPart` (the page says of each of the three: "This parameter can be
  NULL").
- `GetDiskFreeSpaceA`: its page does not say that one of the four out-pointers may be NULL: a
  NULL one is `RT_UNIMPLEMENTED`, checked first. Then `rt_disk_space(name, &avail, &total, NULL)`
  (FALSE: return FALSE, nothing written), and: 8 sectors per cluster, 512 bytes per sector (4 KB
  clusters: what an NTFS volume of this size has, from memory); free clusters `avail / 4096`
  and total clusters `total / 4096` (the page: both are the clusters "available to the user who
  is associated with the calling thread"), each stored as 0xFFFFFFFF if it is larger than that
  (planner's choice: the page does not say what a volume beyond 16 TB reports).
- Tests: `"C:\\"` and a NULL name succeed and agree with each other in the total; the Ex form's
  total equals `rt_disk_space(RT_NAME_A("C:\\"), NULL, &t, NULL)` called by the test, and the old
  form's total clusters equal that value / 4096 (or the cap); free is not above total; the Ex
  form with all three pointers NULL is TRUE; `"D:\\"` and `""` fail with `ERROR_PATH_NOT_FOUND`
  and leave presets as they were; success leaves a preset last error alone; the stops (a file:
  make it with the helper `put` of the note above and name it `C:\\Titanfall2\\<name>`).""")
    row(id="temp-path-a", lib="kernel32", members=["GetTempPathA"], needs=["rt-core-path", "rt-core-small"], stage="B",
        header="rt/path.h", core=["rt_path_known_w", "rt_path_known_a"],
        decl_only=["rt_path_known_a"], model="GetCurrentDirectoryW",
        protos=["DWORD GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer)"],
        notes=CS + """\
- One core call: `return rt_path_known_a(RT_KNOWN_TEMP, lpBuffer, nBufferLength);` (the comment
  above the W form is the specification of both; `RT_KNOWN_TEMP` is a value of the enum
  `RtKnownPath` of `rt/path.h`).
- The core function writes the text, or leaves a buffer that is too small untouched, and
  returns the number the page asks for: the length without the terminator, or the size needed
  with it. It cannot fail and never touches last error. It is fatal for a NULL buffer whose
  size would hold the text: list that as `core: no buffer` in `RT-TRAPS` (status `partial`), add
  no guard, prove it with a `T_TRAPS`.
\
- The answer is `C:\\Users\\Player\\AppData\\Local\\Temp\\` (`docs/RUNTIME.md` 7.1 and 8.2: the
  value of `TMP`, the first variable the page says the function looks at), with the backslash
  at the end that the page requires. The page: the return value is the length without the
  terminator, or, when the buffer is too small, the size needed with it; 0 is a failure, which
  this shim never has.
- Tests: the text and its length in a buffer of `MAX_PATH`; a buffer one byte too small and a
  size of 0 with a NULL buffer return the length plus one, and a preset buffer keeps every
  byte; a buffer of exactly the length plus one; a preset last error stays.""")

    # ---- stage C: the engine ---------------------------------------------------------------------
    row(id="view-map", lib="kernel32", members=["MapViewOfFile", "UnmapViewOfFile"], needs=["rt-file-map"],
        stage="C", header="rt/file.h", core=["rt_view_map", "rt_view_unmap", "rt_section_create"],
        decl_only=["rt_section_create"], model="CreateFileMappingA",
        protos=["LPVOID MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, "
                "DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap)",
                "BOOL UnmapViewOfFile(LPCVOID lpBaseAddress)"],
        notes=FE + FT + CS + """\
- One core call each: `return (LPVOID)(uintptr_t)rt_view_map(hFileMappingObject, dwDesiredAccess,
  (ULONGLONG)dwFileOffsetHigh << 32 | dwFileOffsetLow, dwNumberOfBytesToMap);` and `return
  rt_view_unmap((uint64_t)(uintptr_t)lpBaseAddress);`. The `FILE_MAP_` values are in
  `rt/win/file.h`.
- Tests (sections from `rt_section_create(INVALID_HANDLE_VALUE, PAGE_READWRITE, 131072, NULL)`,
  which read as zeros, and from a file of 10 bytes opened for reading with `PAGE_READONLY`;
  close handles with `rt_handle_close`): a read-write view of the whole section (size 0) takes a
  write and a second view shows it; `FILE_MAP_READ` of the file shows its bytes; an offset of
  65536 with size 4096; an offset that is not a multiple of 64 KB (`ERROR_MAPPED_ALIGNMENT`); a
  handle that is no section (`ERROR_INVALID_HANDLE`); a read-write view of the read-only section
  (`ERROR_ACCESS_DENIED`); an offset beyond the section (`ERROR_ACCESS_DENIED`); the view still
  works after its section handle is closed; `UnmapViewOfFile` returns TRUE once and then FALSE
  with `ERROR_INVALID_ADDRESS`, as for NULL; success leaves last error alone; the stop `core:
  an access that is none of the listed forms` (`FILE_MAP_EXECUTE`).""")
    row(id="memory-status-old", lib="kernel32", members=["GlobalMemoryStatus"], needs=["rt-core-machine"],
        stage="C", header="rt/machine.h", core=["rt_machine"], model="GlobalMemoryStatusEx",
        protos=["void GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer)"],
        notes="""\
- The older form of `GlobalMemoryStatusEx`, which is written: read that shim and compute the
  same numbers from `rt_machine()` (`phys_total`, `phys_avail`, `min_address`, `max_address`):
  `dwMemoryLoad`, `dwTotalPhys`, `dwAvailPhys`, `dwTotalPageFile`, `dwAvailPageFile`,
  `dwTotalVirtual`, `dwAvailVirtual`. `MEMORYSTATUS` (56 bytes, `rt/win/sysinfo.h`) has `SIZE_T`
  members, 8 bytes each in a 64-bit process, so no value is cut.
- `dwLength` is set to 56 by the function; the caller need not set it (the `MEMORYSTATUS`
  page: "the function sets it"). It cannot fail and never touches last error. A NULL `lpBuffer` is
  `RT_UNIMPLEMENTED`. Status `partial`.
- Tests: with `dwLength` preset to 0, every member equals the formula's value and `dwLength` is
  56; the 8 bytes after the structure (0xA5) are untouched; last error is left alone; the NULL
  stop.""")
    row(id="file-time-set", lib="kernel32", members=["SetFileTime"], needs=["core-file-attr-body"], stage="C",
        header="rt/file.h", core=["rt_file_set_times", "rt_file_info_by_handle"],
        decl_only=["rt_file_info_by_handle"], more=[("rt/time.h", ["rt_time_filetime_from_unix"])],
        model="GetFileAttributesExA",
        protos=["BOOL SetFileTime(HANDLE hFile, const FILETIME *lpCreationTime, "
                "const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime)"],
        notes=FE + FT + CS + """\
- One core call: `return rt_file_set_times(hFile, lpCreationTime, lpLastAccessTime,
  lpLastWriteTime);`.
- Tests (times from `rt_time_filetime_from_unix`, read back with `rt_file_info_by_handle`): a
  write time of 2001-09-09 01:46:40 UTC (unix 1000000000) is stored and the access time stays;
  an access time alone; all three pointers NULL changes nothing and returns TRUE; a `FILETIME`
  of 0 leaves that time unchanged; a handle that is not open (`ERROR_INVALID_HANDLE`); success
  leaves last error alone; the stop `core: a creation time that is given and not 0`.""")
    row(id="filetime-to-dos", lib="kernel32", members=["FileTimeToDosDateTime"],
        needs=["rt-core-time", "rt-core-locale"], stage="C", header="rt/locale.h",
        core=["rt_systemtime_from_filetime"], more=[("rt/time.h", ["rt_time_filetime_from_unix"])],
        model="GetSystemTimeAsFileTime",
        protos=["BOOL FileTimeToDosDateTime(const FILETIME *lpFileTime, LPWORD lpFatDate, LPWORD lpFatTime)"],
        notes=NULL_PTR + """\
- The page gives the two formats: the date is day in bits 0-4, month in bits 5-8 and (year -
  1980) in bits 9-15; the time is seconds divided by 2 in bits 0-4, minutes in bits 5-10 and
  hours in bits 11-15. Break the value down with `rt_systemtime_from_filetime`.
- Failures, FALSE with `ERROR_INVALID_PARAMETER` (Wine time.c test_FileTimeToDosDateTime, for
  1601) and nothing written:
  a value of 0x8000000000000000 or more; a year before 1980 or after 2107 (the page: "The
  MS-DOS date format can represent only dates between 1/1/1980 and 12/31/2107; this conversion
  fails if the input file time is outside this range"). No time zone is applied.
- Tests: 2000-01-01 00:00:00 (`rt_time_filetime_from_unix(946684800, 0)`) is date 0x2821 and
  time 0; 2001-09-09 01:46:40 (unix 1000000000) is date 0x2B29 and time 0x0DD4; an odd second
  rounds down (unix 1000000001 gives the same time); 1979-12-31 and value 0 fail with both
  WORDs untouched; success leaves last error alone; each NULL stop.""")
    row(id="priority-boost", lib="kernel32", members=["SetThreadPriorityBoost"], needs=["core-thread-ctl-body"],
        stage="C", header="rt/thread.h", core=["rt_thread_from_handle"],
        more=[("rt/handle.h", ["rt_object_unref"])], model="SetThreadPriority",
        protos=["BOOL SetThreadPriorityBoost(HANDLE hThread, BOOL bDisablePriorityBoost)"],
        notes=ADVISORY + N.THREAD_ERR + """\
- `docs/RUNTIME.md` 6.3: priorities are recorded and otherwise ignored, and this call
  "succeeds". The handle is still checked: `RtObject *o; if (!rt_thread_from_handle(hThread, &o))
  return FALSE;` (the core set `ERROR_INVALID_HANDLE`), then `rt_object_unref(o);`, the log line
  and `return TRUE;`.
- Tests: `(HANDLE)-2` (the calling thread) with TRUE and with FALSE; NULL and an event handle
  (`rt_event_create`, `rt/sync.h`) fail with `ERROR_INVALID_HANDLE`; success leaves last error
  alone; the log line appears once for two calls.""")
    row(id="console-input-state", lib="kernel32", members=["SetConsoleMode", "GetNumberOfConsoleInputEvents"],
        needs=["rt-contract-file"], stage="C", header=None, core=[], model="GetConsoleMode",
        protos=["BOOL SetConsoleMode(HANDLE hConsoleHandle, DWORD dwMode)",
                "BOOL GetNumberOfConsoleInputEvents(HANDLE hConsoleInput, LPDWORD lpcNumberOfEvents)"],
        notes=NO_CONSOLE + """\
- Both: `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);`.
- Tests: each with `(HANDLE)0x44`, with `rt_std_get(STD_INPUT_HANDLE)` (`rt/file.h`) and with
  NULL; a count preset to 0xA5A5A5A5 keeps its value; the last error; the log line.""")
    row(id="console-input-read", lib="kernel32", members=["PeekConsoleInputA", "ReadConsoleInputA"],
        needs=["rt-contract-file"], stage="C", header=None, core=[], model="GetConsoleMode",
        protos=["BOOL PeekConsoleInputA(HANDLE hConsoleInput, LPVOID lpBuffer, DWORD nLength, "
                "LPDWORD lpNumberOfEventsRead)",
                "BOOL ReadConsoleInputA(HANDLE hConsoleInput, LPVOID lpBuffer, DWORD nLength, "
                "LPDWORD lpNumberOfEventsRead)"],
        notes=NO_CONSOLE + """\
- Both: `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);`. `lpBuffer` is `LPVOID` here (the
  page's `PINPUT_RECORD`: the structure is not defined in this runtime and never written).
- Tests: each with `(HANDLE)0x44` and with every pointer NULL; a buffer of 40 bytes and a count
  preset to a pattern keep their values; the last error; the log line.""")
    row(id="console-input-flush", lib="kernel32", members=["FlushConsoleInputBuffer"],
        needs=["rt-contract-file"], stage="C", header=None, core=[], model="GetConsoleMode",
        protos=["BOOL FlushConsoleInputBuffer(HANDLE hConsoleInput)"],
        notes=NO_CONSOLE + """\
- `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);`.
- Tests: `(HANDLE)0x44`, `rt_std_get(STD_INPUT_HANDLE)` (`rt/file.h`) and NULL; the last error;
  the log line.""")
    row(id="systemtime-to-filetime", lib="kernel32", members=["SystemTimeToFileTime", "DosDateTimeToFileTime"],
        needs=["rt-core-time", "rt-core-locale", "rt-core-small"], stage="C", header="rt/locale.h",
        core=["rt_filetime_from_systemtime"], model="GetSystemTimeAsFileTime",
        protos=["BOOL SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime)",
                "BOOL DosDateTimeToFileTime(WORD wFatDate, WORD wFatTime, LPFILETIME lpFileTime)"],
        notes=NULL_PTR + """\
- `SystemTimeToFileTime`: after the NULL checks, `uint64_t ft; if
  (!rt_filetime_from_systemtime(lpSystemTime, &ft)) return FALSE;` (the core has set
  `ERROR_INVALID_PARAMETER`; the shim sets nothing and writes nothing), then store the low half
  in `dwLowDateTime` and the high half in `dwHighDateTime` and return TRUE.
- `DosDateTimeToFileTime`: unpack the two WORDs (the date is day in bits 0-4, month in bits
  5-8 and (year - 1980) in bits 9-15; the time is seconds divided by 2 in bits 0-4, minutes in
  bits 5-10 and hours in bits 11-15) into a `SYSTEMTIME` with `wMilliseconds` 0 and call the core
  the same way: a date or time that does not exist (month 0, day 0, 30 seconds-units and more,
  hour 24 and more) is the core's refusal. No time zone is applied.
- Tests, with the values of the core's comment: 1980-01-01 00:00:00.000 is low 0xE1D58000 and
  high 0x01A8E79F in both functions (as DOS: date 0x0021, time 0); 2002-07-26 11:55:32.123 is
  low 0x580716B0, high 0x01C2349B; DOS date 0x2B29 with time 0x0DD4 (2001-09-09 01:46:40) is
  the 64-bit value 0x01C138D144FF8000; month 0 and hour 24 fail with `ERROR_INVALID_PARAMETER`
  and leave a preset `FILETIME` as it was (as DOS: date 0x0001, and time 0xC000); `wDayOfWeek`
  is ignored (two values, one result); success leaves a preset last error alone; each NULL stop.""")
    row(id="tz-local-time", lib="kernel32", members=["SystemTimeToTzSpecificLocalTime"],
        needs=["rt-core-time", "rt-core-locale", "rt-core-small"], stage="C", header="rt/locale.h",
        core=["rt_filetime_from_systemtime", "rt_systemtime_from_filetime"],
        more=[("rt/time.h", ["rt_time_zone_bias_minutes"])], model="GetSystemTimeAsFileTime",
        protos=["BOOL SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION *lpTimeZoneInformation, "
                "const SYSTEMTIME *lpUniversalTime, LPSYSTEMTIME lpLocalTime)"],
        notes=NULL_PTR + """\
- The NULL note applies to `lpUniversalTime` and `lpLocalTime`; `lpTimeZoneInformation` may be
  NULL ("the function uses the currently active time zone", the page).
- The universal time to a 64-bit value with `rt_filetime_from_systemtime` (FALSE if it refuses:
  the core has set `ERROR_INVALID_PARAMETER`; nothing is written), minus the bias in minutes
  times 600,000,000, back with `rt_systemtime_from_filetime`. The bias is
  `rt_time_zone_bias_minutes()` for a NULL zone; for a zone given by the caller it is the zone's
  `Bias` if both of its transition dates have `wMonth` 0 (no daylight rule), else
  `RT_UNIMPLEMENTED`. A local time that would lie before 1601-01-01 or after the year 30827
  (the value would be negative, or 0x7FFF35F4F06C58F0 or more) is `RT_UNIMPLEMENTED`.
- Tests: a zone with `Bias` 300 and no transition dates turns 2002-05-01 12:00:00.000 into
  2002-05-01 07:00:00.000 with `wDayOfWeek` 3 (a Wednesday); `Bias` -60 turns 2001-12-31
  23:30:00 into 2002-01-01 00:30:00 (a Tuesday, 2); a NULL zone gives the same members as a
  zone whose `Bias` is `rt_time_zone_bias_minutes()`; a universal time with month 13 fails with
  `ERROR_INVALID_PARAMETER` and leaves a preset output as it was; success leaves a preset last
  error alone; the stops (a zone with `StandardDate.wMonth` 10; each NULL pointer).""")
    row(id="system-dir", lib="kernel32", members=["GetSystemDirectoryA", "GetSystemDirectoryW"],
        needs=["rt-core-path", "rt-core-small"], stage="C", header="rt/path.h", core=["rt_path_known_w", "rt_path_known_a"],
        decl_only=["rt_path_known_a"],
        model="GetCurrentDirectoryW",
        protos=["UINT GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize)",
                "UINT GetSystemDirectoryW(LPWSTR lpBuffer, UINT uSize)"],
        notes=CS + """\
- One core call each: `return rt_path_known_w(RT_KNOWN_SYSTEM, lpBuffer, uSize);` and, in the A
  shim, `return rt_path_known_a(RT_KNOWN_SYSTEM, lpBuffer, uSize);` (the comment above the W
  form is the specification of both; `RT_KNOWN_SYSTEM` is a value of the enum `RtKnownPath` of
  `rt/path.h`).
- The core function writes the text, or leaves a buffer that is too small untouched, and
  returns the number the page asks for: the length without the terminator, or the size needed
  with it. It cannot fail and never touches last error. It is fatal for a NULL buffer whose
  size would hold the text: list that as `core: no buffer` in `RT-TRAPS` (status `partial`), add
  no guard, prove it with a `T_TRAPS`.
\
- The answer is `C:\\Windows\\system32` (`docs/RUNTIME.md` 7.1), with no backslash at the end.
  The page: the return value is the length copied without the terminator, or, if the buffer is
  too small, the size needed with it.
- Tests, per function: the text and its length in a buffer of `MAX_PATH`; a buffer one unit
  too small, and a NULL buffer with size 0, return the length plus one and a preset buffer
  keeps every unit; a buffer of exactly the length plus one; a preset last error stays.""")
    row(id="expand-env-a", lib="kernel32", members=["ExpandEnvironmentStringsA"], needs=["rt-core-env", "rt-core-small"],
        stage="C", header="rt/env.h", core=["rt_env_expand_a", "rt_env_set_a"], decl_only=["rt_env_set_a"],
        model="SetPriorityClass",
        protos=["DWORD ExpandEnvironmentStringsA(LPCSTR lpSrc, LPSTR lpDst, DWORD nSize)"],
        notes=CS + """\
- One core call: `return rt_env_expand_a(lpSrc, lpDst, nSize);`. Unlike the other functions of
  `rt/env.h` it takes no `DWORD *err`: it cannot fail, never returns 0 and never touches last
  error. Status `partial`: the core's fatal cases are the `RT-TRAPS` entries (`core: ...`).
- Tests (values of Wine's test, which the core's comment names): after
  `rt_env_set_a("TVAR", "WINE", &err)`, on a buffer preset to `"abcdefghijklmnopqrstuv"`:
  `"%TVAR% long long"` with size 16 returns 15 and gives `"WINE long long"`; with size 15 it
  returns 16 and the buffer is an empty string (only its first byte changed); with size 0 the
  same; `"%TVAR2% long long"` (not set) with size 19 returns 18 and is copied as it is;
  `"%TVAR long long"` with size 17 returns 16 and is copied as it is; a NULL buffer with size 0
  returns the size (16 for the first string); a last error preset to 0xdeadbeef stays in every
  call, also when the buffer is too small. One `T_TRAPS` per listed stop: a NULL source,
  `"caf\\xE9"`, and `"%NOPE%TVAR%"`.""")
    row(id="process-times", lib="kernel32", members=["GetProcessTimes"], needs=["core-process-body", "rt-core-small"],
        stage="C", header="rt/thread.h", core=["rt_process_times"], model="GetExitCodeProcess",
        protos=["BOOL GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, "
                "LPFILETIME lpKernelTime, LPFILETIME lpUserTime)"],
        notes=NULL_PTR + """\
- The four pointers are checked first (a NULL one is `RT_UNIMPLEMENTED`, as the note above
  says; the core would stop on three of them too, with a text that does not name the
  argument). Then one core call for the three times it gives: `if (!rt_process_times(hProcess,
  lpCreationTime, lpKernelTime, lpUserTime)) return FALSE;` (the core has set
  `ERROR_INVALID_HANDLE` and written nothing). Then the exit time, which the page calls
  "undefined" for a process that has not exited: store 0 in both of its DWORDs. Return TRUE.
- Tests: `(HANDLE)-1` gives TRUE, a creation time that is not 0, an exit time of 0 0 over a
  preset pattern, and a preset last error that stays; a second call gives the same creation
  time; an event handle (`CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, NULL)`) gives FALSE with
  `ERROR_INVALID_HANDLE` and leaves all four presets as they were; one `T_TRAPS` per NULL pointer.""")
    row(id="error-mode", lib="kernel32", members=["SetErrorMode"], needs=["core-process-body", "rt-core-small"],
        stage="C", header="rt/thread.h", core=["rt_process_swap_error_mode"], model="SetPriorityClass",
        protos=["UINT SetErrorMode(UINT uMode)"],
        notes=CS + """\
- `docs/RUNTIME.md` 9.2 names this call `advisory`: no error box exists to switch off. The page:
  "The return value is the previous state of the error-mode bit flags", and callers restore
  that value, so the core stores it (one process-wide value, 0 at the start). One core call:
  `return rt_process_swap_error_mode(uMode);`. It cannot fail and never touches last error.
- Status `partial`, because the core stops on `SEM_NOALIGNMENTFAULTEXCEPT` and on a bit outside
  the four `SEM_` values (`rt/win/process.h`): list both as `core: ...` in `RT-TRAPS`, add no
  guard, prove each with a `T_TRAPS`. No log line is needed.
- Tests: 0 at first; each call returns what the one before set (use `SEM_FAILCRITICALERRORS`,
  `SEM_NOGPFAULTERRORBOX`, `SEM_NOOPENFILEERRORBOX` and 0); a preset last error stays.""")

    # ---- stage D: the engine's subsystems --------------------------------------------------------
    row(id="fls-alloc", lib="kernel32", members=["FlsAlloc", "FlsFree"], needs=["rt-contract-thread"], stage="D",
        header="rt/thread.h", core=["rt_fls_alloc", "rt_fls_free", "rt_fls_set", "rt_fls_get"],
        decl_only=["rt_fls_set", "rt_fls_get"], model="TlsAlloc",
        protos=["DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback)", "BOOL FlsFree(DWORD dwFlsIndex)"],
        notes=N.THREAD_ERR + CS + """\
- One core call each: `return rt_fls_alloc(lpCallback);` and `return rt_fls_free(ctx,
  dwFlsIndex);`. The C runtimes of three middleware DLLs call these at start-up.
- Tests: two allocations give two different indexes, neither 0, last error left alone; a freed
  index is given out again; `FlsFree` of index 0, of 128 and of an index that is not allocated
  fail with `ERROR_INVALID_PARAMETER`; with a callback written with `GUEST_FN` (`rt/test.h`: it
  stores `ctx->r[1]` in a static) and a value set with `rt_fls_set(T_CTX(), i, 0x1234)`,
  `FlsFree` runs the callback once with 0x1234; with a value of 0 it does not run; an index
  allocated with a NULL callback is freed without a call. Free every index.""")
    row(id="fls-value", lib="kernel32", members=["FlsGetValue", "FlsSetValue"], needs=["rt-contract-thread"],
        stage="D", header="rt/thread.h", core=["rt_fls_get", "rt_fls_set", "rt_fls_alloc", "rt_fls_free"],
        decl_only=["rt_fls_alloc", "rt_fls_free"], model="TlsGetValue",
        protos=["PVOID FlsGetValue(DWORD dwFlsIndex)", "BOOL FlsSetValue(DWORD dwFlsIndex, PVOID lpFlsData)"],
        notes=N.THREAD_ERR + """\
- One core call each: `return (PVOID)(uintptr_t)rt_fls_get(ctx, dwFlsIndex);` and `return
  rt_fls_set(ctx, dwFlsIndex, (uint64_t)(uintptr_t)lpFlsData);`. One exception to the rule
  above, stated in the core's comment: `rt_fls_get` sets last error to `ERROR_SUCCESS` when it
  succeeds, and the shim leaves that as it is.
- Tests (indexes from `rt_fls_alloc(0)`, freed with `rt_fls_free` at the end): a new index
  reads NULL with last error `ERROR_SUCCESS` (after a preset of 0xDEADBEEF); a value of 0x217
  set is read back with last error `ERROR_SUCCESS`, on two indexes independently;
  `FlsSetValue` leaves last error alone on success; index 0 and index 128 fail in both
  functions with `ERROR_INVALID_PARAMETER`; an index in range that was never allocated: the
  setter returns TRUE and the getter then returns the value, as the core's comment says
  (Windows's behaviour by Wine's test). No stop: status `full`.""")
    row(id="env-strings-a", lib="kernel32", members=["GetEnvironmentStrings", "FreeEnvironmentStringsA"],
        needs=["rt-core-env"], stage="D", header="rt/env.h",
        core=["rt_env_block_w", "rt_env_block_a", "rt_env_block_free", "rt_env_set_a"], decl_only=["rt_env_set_a"],
        model="GetEnvironmentStringsW",
        protos=["LPCH GetEnvironmentStrings(void)", "BOOL FreeEnvironmentStringsA(LPCH penv)"],
        notes=CS + """\
- The A twins of `GetEnvironmentStringsW` and `FreeEnvironmentStringsW`, which are written.
  `DWORD err = 0; CHAR *b = rt_env_block_a(&err); if (!b) return FAIL(ctx, err, NULL); return b;`
  and `return rt_env_block_free(penv);`.
- Tests: after `rt_env_set_a("RT_T_A", "one")` the block contains the entry `RT_T_A=one`, every
  entry has a `=` after its first character, and the block ends with two NULs; two calls give
  two different blocks; a variable set after the call is not in the earlier block; freeing
  returns TRUE, as does freeing NULL; last error is left alone; the stop `core: freeing a
  pointer that is not a live block`.""")
    row(id="startup-info-a", lib="kernel32", members=["GetStartupInfoA"], needs=["rt-core-env"], stage="D",
        header="rt/env.h", core=["rt_env_startup_info"], model="GetStartupInfoW",
        protos=["void GetStartupInfoA(LPSTARTUPINFOA lpStartupInfo)"],
        notes="""\
- The A twin of `GetStartupInfoW`, which is written. The core gives the W structure, whose
  three strings are NULL: call `rt_env_startup_info` into a local `STARTUPINFOW`, zero the 104
  bytes of `*lpStartupInfo` and copy every member that is not a string (`STARTUPINFOA`,
  `rt/win/process.h`, has the same layout; `cb` is 104 in both). If one of the three strings of
  the W structure is not NULL, `RT_UNIMPLEMENTED` (it would need a conversion). A NULL
  `lpStartupInfo` is `RT_UNIMPLEMENTED`. Status `partial`.
- Tests: on a structure preset to 0xA5, `cb` is 104, the three strings and the three standard
  handles are NULL, `dwFlags` and `wShowWindow` are 0, and all 104 bytes equal what
  `rt_env_startup_info` wrote into a zeroed `STARTUPINFOW` (compare with `memcmp`); last error is
  left alone; the NULL stop.""")
    row(id="heap-set-info", lib="kernel32", members=["HeapSetInformation"], needs=["rt-core-heap"], stage="D",
        header="rt/heap.h", core=["rt_heap_validate", "rt_heap_process"], model="HeapValidate",
        protos=["BOOL HeapSetInformation(HANDLE HeapHandle, HEAP_INFORMATION_CLASS HeapInformationClass, "
                "PVOID HeapInformation, SIZE_T HeapInformationLength)"],
        notes="""\
- `docs/RUNTIME.md` 5.4: "`HeapSetInformation` succeeds and does nothing." The classes are in
  `rt/win/memory.h`; what is accepted, from the page:
  `HeapEnableTerminationOnCorruption` (1): `HeapHandle` may be NULL and the two other arguments
  are not looked at; TRUE. `HeapCompatibilityInformation` (0): `HeapInformationLength` must be
  4 and the ULONG at `HeapInformation` 2 (the low-fragmentation heap), on a heap for which
  `rt_heap_validate(HeapHandle, 0, NULL)` is TRUE; TRUE. `HeapOptimizeResources` (3) and any
  other class, a compatibility value other than 2, another length, a NULL `HeapInformation`
  for class 0, or a handle that is not a live heap for class 0: `RT_UNIMPLEMENTED` with the
  values (what Windows answers is not verified). Status `partial` (decided by the orchestrator, 2026-10-10: the generator
  refuses stops under status `advisory`): the two accepted classes are the cases, each stop
  is an `RT-TRAPS` entry. The accepted calls change nothing in the runtime and log one line,
  once.
- Tests: class 1 with (NULL, NULL, 0) and with the process heap; class 0 with the value 2 on
  `rt_heap_process()`; last error left alone; the log line once; the stops (class 3, value 1,
  length 8).""")
    row(id="handle-count", lib="kernel32", members=["SetHandleCount"], needs=["rt-contract-file"], stage="D",
        header=None, core=[], model="GetTickCount",
        protos=["UINT SetHandleCount(UINT uNumber)"],
        notes="""\
- `return uNumber;`, status `full`, `RT-TRAPS: none`. The page: "Changes the number of file
  handles available to a process... For Windows Win32 systems, this API has no effect." The
  return value is not on the page; Windows returns the number asked for (from memory; the C
  runtimes that call it at start-up ignore it). No log line: there is nothing to say.
- Tests: 0, 20, 255 and 0xFFFFFFFF come back; last error is left alone.""")
    row(id="local-alloc", lib="kernel32", members=["LocalAlloc", "LocalFree"], needs=["rt-core-heap"], stage="D",
        header="rt/heap.h", core=["rt_heap_alloc", "rt_heap_free", "rt_heap_process", "rt_heap_size"],
        model="HeapAlloc",
        protos=["HLOCAL LocalAlloc(UINT uFlags, SIZE_T uBytes)", "HLOCAL LocalFree(HLOCAL hMem)"],
        notes=CS + """\
- Fixed memory only: the page says `LMEM_FIXED` "Allocates fixed memory. The return value is a
  pointer to the memory object", and `docs/RUNTIME.md` 5.4 puts such memory in the process
  heap. `LocalAlloc`: a flag outside `LMEM_ZEROINIT` (`rt/win/memory.h`; `LMEM_MOVEABLE` too) is
  `RT_UNIMPLEMENTED` with the flags; then `p = rt_heap_alloc(rt_heap_process(), uFlags &
  LMEM_ZEROINIT ? HEAP_ZERO_MEMORY : 0, uBytes); if (!p) return FAIL(ctx,
  ERROR_NOT_ENOUGH_MEMORY, NULL); return p;` (the page names that code).
- `LocalFree`: NULL returns NULL ("LocalFree ignores the parameter and returns NULL"); otherwise
  `rt_heap_free(rt_heap_process(), 0, hMem); return NULL;` ("If the function succeeds, the return
  value is NULL"). A pointer that is not a live block of the process heap is the core's stop.
  Status `partial`.
- Tests: `LMEM_FIXED` 16 bytes is writable and `rt_heap_size` says 16; `LPTR` 64 bytes are
  zeros; 0 bytes gives a pointer; each is freed with NULL returned; `LocalFree(NULL)`; last
  error left alone; the stops for `LMEM_MOVEABLE` and `core: a pointer that is not a live
  block`.""")
    row(id="lstrcmp-a", lib="kernel32", members=["lstrcmpA"], needs=["rt-core-locale"], stage="D",
        header="rt/locale.h", core=["rt_compare_string", "rt_mb_to_wide", "rt_locale_default"],
        decl_only=["rt_mb_to_wide"], model="CompareStringW",
        protos=["int lstrcmpA(LPCSTR lpString1, LPCSTR lpString2)"],
        notes=A_TEXT + """\
- The page: "The function calls CompareStringEx, using the current thread locale, and subtracts
  2 from the result". So: convert both strings (with their terminators, `n` = -1) into two
  `WCHAR[1024]`, `r = rt_compare_string(rt_locale_default(), 0, w1, -1, w2, -1)`, return `r - 2`
  (-1, 0 or 1). The core compares code units, not Windows's word sort (its comment says so):
  equality is exact, the order of unequal strings may differ from Windows. Say so in a comment.
- NULL strings (not on the page; Wine locale.c test_CompareStringA asserts them): both NULL 0,
  only the first NULL
  -1, only the second 1; no conversion and no core call.
- Tests: equal strings 0; `"a"` against `"b"` -1 and the reverse 1; a prefix sorts first; the
  empty string against `"a"`; the three NULL cases; last error left alone; the stop for a
  string of 1,100 characters.""")
    row(id="string-type-a", lib="kernel32", members=["GetStringTypeA"], needs=["rt-core-locale"], stage="D",
        header="rt/locale.h", core=["rt_string_type", "rt_mb_to_wide"], decl_only=["rt_mb_to_wide"],
        model="GetStringTypeW",
        protos=["BOOL GetStringTypeA(LCID Locale, DWORD dwInfoType, LPCSTR lpSrcStr, int cchSrc, "
                "LPWORD lpCharType)"],
        notes=N.LOCALE_ERR + A_TEXT + """\
- `Locale` only selects the code page of the text. 0x0409, `LOCALE_USER_DEFAULT` (0x0400) and
  `LOCALE_SYSTEM_DEFAULT` (0x0800) mean code page 1252 here; any other value is
  `RT_UNIMPLEMENTED` with the value (an old C runtime may pass 0: the stop will say so).
- Convert `cchSrc` bytes (`-1`: up to and including the terminator) as above, then `return
  rt_string_type(dwInfoType, wide, n, lpCharType);`: one WORD per byte. `lpSrcStr` NULL or
  `cchSrc` 0: `ERROR_INVALID_PARAMETER`, FALSE, before the conversion. Status `partial`.
- Tests: `CT_CTYPE1` of `"a1 "` with length 3 equals what `rt_string_type` gives for `L"a1 "`;
  length -1 also writes the terminator's WORD; byte 0xE9 (e acute in code page 1252) is the
  type of U+00E9; an info type of 0 (`ERROR_INVALID_PARAMETER`); a WORD after the output
  untouched; success leaves last error alone; the stops for locale 0x0407 and for 1,100 bytes.""")
    row(id="lcmap-a", lib="kernel32", members=["LCMapStringA"], needs=["rt-core-locale"], stage="D",
        header="rt/locale.h", core=["rt_lcmap_string", "rt_mb_to_wide", "rt_wide_to_mb"],
        decl_only=["rt_mb_to_wide", "rt_wide_to_mb"], model="LCMapStringW",
        protos=["int LCMapStringA(LCID Locale, DWORD dwMapFlags, LPCSTR lpSrcStr, int cchSrc, LPSTR lpDestStr, "
                "int cchDest)"],
        notes=N.LOCALE_ERR + A_TEXT + CS + """\
- For `LCMAP_LOWERCASE` and `LCMAP_UPPERCASE`, which is all the core maps. Steps: (1)
  `lpSrcStr` NULL, `cchSrc` 0 or `cchDest` negative: 0 with `ERROR_INVALID_PARAMETER`. (2)
  Convert the source (`cchSrc`, or -1 with the terminator) into `wide`, n WCHARs. (3) `m =
  rt_lcmap_string(Locale, dwMapFlags, wide, n, mapped, 1024)` into a second `WCHAR[1024]`; 0 is
  the failure with the core's code. (4) `cchDest` 0: return m (the size needed: one byte per
  WCHAR). (5) `cchDest < m`: 0 with `ERROR_INSUFFICIENT_BUFFER`, nothing written (the W form
  writes the part that fits; for the A form that is not verified, so nothing is). (6) Convert
  m WCHARs into `lpDestStr` as above and return m. `lpDestStr == lpSrcStr` is allowed.
  Status `partial`.
- Tests: `"Hello"` to upper and to lower with length 5 (no terminator written: a byte after
  the output is untouched) and with -1; byte 0xE9 to upper is 0xC9; the size query; a buffer
  one byte too small; in-place mapping; the invalid cases of step 1; success leaves last error
  alone; the stops: 1,100 bytes, and `core: a locale other than en-US` (0x0407).""")
    row(id="locale-info-a", lib="kernel32", members=["GetLocaleInfoA"], needs=["rt-core-locale"], stage="D",
        header="rt/locale.h", core=["rt_locale_info", "rt_wide_to_mb"], decl_only=["rt_wide_to_mb"],
        model="GetLocaleInfoW",
        protos=["int GetLocaleInfoA(LCID Locale, LCTYPE LCType, LPSTR lpLCData, int cchData)"],
        notes=N.LOCALE_ERR + A_TEXT + CS + """\
- Steps: (1) `cchData` negative, or `lpLCData` NULL with `cchData` not 0: 0 with
  `ERROR_INVALID_PARAMETER`. (2) With `LOCALE_RETURN_NUMBER` in `LCType`: the number is a DWORD
  in both forms; `rt_locale_info(Locale, LCType, (WCHAR *)&value, 2)` into a local DWORD; on
  success `cchData` 0 returns 4, `cchData < 4` is 0 with `ERROR_INSUFFICIENT_BUFFER`, else
  copy the 4 bytes and return 4 (sizes are in bytes for the A form: from memory, not verified).
  (3) Otherwise `m = rt_locale_info(Locale, LCType, wide, 256)` into a `WCHAR[256]` (0: the
  failure with the core's code; `ERROR_INSUFFICIENT_BUFFER` here is `RT_UNIMPLEMENTED`: no
  entry of the table is that long). (4) `cchData` 0: return m. (5) `cchData < m`: 0 with
  `ERROR_INSUFFICIENT_BUFFER`, nothing written. (6) Convert the m WCHARs (terminator included)
  into `lpLCData` and return m. Status `partial`.
- Tests, each against `rt_locale_info` as the oracle: `LOCALE_SDECIMAL` and
  `LOCALE_SENGLANGUAGE` as text (same length, same characters); the size query; a buffer one
  byte too small leaves a preset buffer alone; `LOCALE_IDEFAULTANSICODEPAGE |
  LOCALE_RETURN_NUMBER` stores 1252 in 4 bytes; the invalid cases of step 1; success leaves
  last error alone; the stop `core: a locale other than en-US` (0x0407).""")
    row(id="verify-version-w", lib="kernel32", members=["VerifyVersionInfoW"], needs=["rt-core-machine"],
        stage="D", header="rt/machine.h", core=["rt_machine"], model="VerifyVersionInfoA",
        protos=["BOOL VerifyVersionInfoW(LPOSVERSIONINFOEXW lpVersionInformation, DWORD dwTypeMask, "
                "DWORDLONG dwlConditionMask)"],
        notes="""\
- The W twin of `VerifyVersionInfoA`, which is written: read that shim and write the same
  function for `OSVERSIONINFOEXW` (`rt/win/sysinfo.h`, 284 bytes: `dwOSVersionInfoSize` must be
  284 where the A form asks for 156). Every member the comparison reads is a number at a
  different offset; `szCSDVersion` is the only member that differs in type and is never
  compared. Same cases, same stops, same messages with the name `VerifyVersionInfoW`.
- Tests: the tests of `VerifyVersionInfoA` with the W structure, building each condition mask
  with `CALL(kernel32, VerSetConditionMask, ...)` as that test does.""")
    row(id="global-alloc", lib="kernel32", members=["GlobalAlloc", "GlobalFree"], needs=["rt-core-heap"], stage="D",
        header=None, core=[], model="HeapAlloc",
        hold="core: heap-global (movable memory objects: handle, lock count, size); with the clipboard",
        protos=["HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes)", "HGLOBAL GlobalFree(HGLOBAL hMem)"],
        notes=HELD_CORE + """\
- Only `vgui2` imports the five `Global` functions (21 sites), around the clipboard, which
  hands memory over as `GMEM_MOVEABLE` handles. A handle of that kind is not a pointer
  (`GlobalLock` gives the pointer), so these wait for a core piece and for the clipboard's
  contract. `GMEM_FIXED` alone would be the row `local-alloc` again.""")
    row(id="global-lock", lib="kernel32", members=["GlobalLock", "GlobalUnlock"], needs=["rt-core-heap"], stage="D",
        header=None, core=[], model="HeapAlloc",
        hold="core: heap-global (movable memory objects); with the clipboard",
        protos=["LPVOID GlobalLock(HGLOBAL hMem)", "BOOL GlobalUnlock(HGLOBAL hMem)"],
        notes=HELD_CORE + """\
- From the page of `GlobalUnlock`: the result is nonzero while the object is still locked, and
  zero with last error `NO_ERROR` when the count reached zero; unlocking an object that is not
  locked is zero with `ERROR_NOT_LOCKED`; for fixed memory it returns TRUE.""")
    row(id="global-size", lib="kernel32", members=["GlobalSize"], needs=["rt-core-heap"], stage="D",
        header=None, core=[], model="HeapSize",
        hold="core: heap-global (movable memory objects); with the clipboard",
        protos=["SIZE_T GlobalSize(HGLOBAL hMem)"],
        notes=HELD_CORE + """\
- The size in bytes of the object, 0 on failure (the page).""")

    out += small_libraries(N)
    return out


def small_libraries(N):
    """Part 2: one row per proposal of the handoff's table.  Released by the orchestrator on
    2026-10-10 (ORC is kept as the text a row would be held with); a row waiting for a core piece
    keeps its own hold."""
    out = []
    ORC = "orchestrator: proposal, see the small-library table of docs/handoff/rt-shim-rows-2.md"

    def row(**kw):
        out.append(kw)

    REG = """\
- The functions of `rt/registry.h` never touch last error and return the status that the `Reg`
  function itself returns: the shim is `return rt_reg_...;` and sets nothing. The core's comment
  is the specification. A test works under a key of its own, `HKEY_CURRENT_USER\\Software\\RtShimTest`
  (constants in `rt/win/registry.h`; every test has a fresh registry in its state directory);
  `runtime/tests/core/registry_ansi.c` shows the calls (read its first 60 lines only).
"""
    # ---- advapi32: the registry (core: rt/registry.h) -----------------------------------------
    row(id="reg-open-a", lib="advapi32", members=["RegOpenKeyExA", "RegCloseKey"], needs=["rt-core-registry"],
        stage="A", header="rt/registry.h", core=["rt_reg_open_key_ex_w", "rt_reg_open_key_ex_a", "rt_reg_close_key",
                                                 "rt_reg_create_key_ex_a"],
        decl_only=["rt_reg_create_key_ex_a"], model="CloseHandle",
        protos=["LSTATUS RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, "
                "PHKEY phkResult)", "LSTATUS RegCloseKey(HKEY hKey)"],
        notes=PROPOSAL + REG + N.CORE_STOPS + """\
- One core call each.
- Tests: a key made with `rt_reg_create_key_ex_a` opens (`ERROR_SUCCESS`, a handle that is not
  the parent) and closes; a key that does not exist is `ERROR_FILE_NOT_FOUND` and leaves a
  preset `*phkResult` as the core's comment says; a second close of the same handle is
  `ERROR_INVALID_HANDLE`; closing a predefined key (`HKEY_CURRENT_USER`) is what the core's
  comment says; last error is never touched (preset 0x1234 stays), also on failure.""")
    row(id="reg-query-a", lib="advapi32", members=["RegQueryValueExA"], needs=["rt-core-registry"], stage="A",
        header="rt/registry.h", core=["rt_reg_query_value_ex_w", "rt_reg_query_value_ex_a", "rt_reg_set_value_ex_a",
                                      "rt_reg_create_key_ex_a"],
        decl_only=["rt_reg_set_value_ex_a", "rt_reg_create_key_ex_a"], model="CloseHandle",
        protos=["LSTATUS RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, "
                "LPBYTE lpData, LPDWORD lpcbData)"],
        notes=PROPOSAL + REG + N.CORE_STOPS + """\
- One core call. This is what the launcher and the engine read their settings with; a value
  that is not there is `ERROR_FILE_NOT_FOUND`, which the core logs once per name.
- Tests (values stored with `rt_reg_set_value_ex_a`): a `REG_SZ` and a `REG_DWORD` come back
  with type, data and size; the size query (`lpData` NULL); a buffer too small
  (`ERROR_MORE_DATA` with the size needed); a value that does not exist; a handle that is not a
  key; last error is never touched.""")
    row(id="reg-create-a", lib="advapi32", members=["RegCreateKeyExA"], needs=["rt-core-registry"], stage="C",
        header="rt/registry.h", core=["rt_reg_create_key_ex_w", "rt_reg_create_key_ex_a", "rt_reg_close_key"],
        decl_only=["rt_reg_close_key"], model="CloseHandle",
        protos=["LSTATUS RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, "
                "REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, "
                "LPDWORD lpdwDisposition)"],
        notes=PROPOSAL + REG + N.CORE_STOPS + """\
- One core call, the arguments in the same order.
- Tests: a new key reports `REG_CREATED_NEW_KEY`, the same call again `REG_OPENED_EXISTING_KEY`;
  a path of two new components creates both; `lpdwDisposition` NULL; the failures the core's
  comment lists that a test can reach; last error is never touched. Close every key.""")
    row(id="reg-set-a", lib="advapi32", members=["RegSetValueExA", "RegDeleteValueA"], needs=["rt-core-registry"],
        stage="A", header="rt/registry.h",
        core=["rt_reg_set_value_ex_w", "rt_reg_set_value_ex_a", "rt_reg_delete_value_w", "rt_reg_delete_value_a",
              "rt_reg_query_value_ex_a", "rt_reg_create_key_ex_a"],
        decl_only=["rt_reg_query_value_ex_a", "rt_reg_create_key_ex_a"], model="CloseHandle",
        protos=["LSTATUS RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, "
                "const BYTE *lpData, DWORD cbData)", "LSTATUS RegDeleteValueA(HKEY hKey, LPCSTR lpValueName)"],
        notes=PROPOSAL + REG + N.CORE_STOPS + """\
- One core call each.
- Tests (read back with `rt_reg_query_value_ex_a`): a `REG_SZ`, a `REG_DWORD` and a `REG_BINARY`
  are stored; a second set replaces the first; the default value (name NULL); a deleted value
  is gone and a second delete is `ERROR_FILE_NOT_FOUND`; a handle that is not a key; last error
  is never touched.""")

    # ---- winmm ----------------------------------------------------------------------------------
    row(id="mm-time", lib="winmm", members=["timeGetTime", "timeEndPeriod"], needs=["rt-core-time"], stage="D",
        header="rt/time.h", core=["rt_time_tick_ms"], model="winmm/timeBeginPeriod",
        protos=["DWORD timeGetTime(void)", "MMRESULT timeEndPeriod(UINT uPeriod)"],
        notes=PROPOSAL + """\
- `timeGetTime`: `return (DWORD)rt_time_tick_ms();` (`docs/RUNTIME.md` 8.1 and the header's
  first comment name this function), status `full`. Tests: two calls do not go backwards; the
  value equals `(DWORD)rt_time_tick_ms()` read just before and after within 1,000 ms; last
  error is left alone.
- `timeEndPeriod`: the twin of the written `timeBeginPeriod` (read both of its files): status
  `advisory`, a period of 0 returns `TIMERR_NOCANDO`, any other returns `TIMERR_NOERROR`, logged
  once as `timeEndPeriod(%u): accepted and ignored`. The page: "TIMERR_NOCANDO if the
  resolution specified in uPeriod is out of range"; whether a call matches an earlier
  `timeBeginPeriod` is not checked. The same tests as `timeBeginPeriod`.""")
    NODEV = NOTHING_THERE + """\
- What is not there: an audio device (`docs/RUNTIME.md` 11.1: audio is not built before Gate 5).
  The `MMSYSERR_` values are in `rt/win/error.h`, the handle types in `rt/win/mm.h`. These
  functions return their result and never touch last error.
"""
    row(id="wave-out-open", lib="winmm", members=["waveOutOpen"], needs=["rt-core-time"], stage="D",
        header=None, core=[], model="AllocConsole",
        protos=["MMRESULT waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCVOID pwfx, DWORD_PTR dwCallback, "
                "DWORD_PTR dwInstance, DWORD fdwOpen)"],
        notes=PROPOSAL + NODEV + """\
- With no output device every identifier is out of range, and the mapper has nothing to
  choose from. From the page's list of results: `uDeviceID == WAVE_MAPPER` returns
  `MMSYSERR_NODRIVER` ("No device driver is present"), any other identifier
  `MMSYSERR_BADDEVICEID` ("Specified device identifier is out of range"). Which of the two the
  mapper gives on a Windows without a sound card is from memory of Wine's `winmm/tests/wave.c`,
  which accepts both. `*phwo` is not written; `pwfx` (`LPCVOID` here: the page's
  `LPCWAVEFORMATEX`) is not read. Log line: `waveOutOpen: there is no audio output device`.
- Tests: `WAVE_MAPPER`, device 0 and device 7; a preset `*phwo` keeps its value; a NULL `phwo`
  (the format query) answers the same; last error is left alone; the log line.""")
    for rid, a, b, protos in [
            ("wave-out-close", "waveOutClose", "waveOutReset",
             ["MMRESULT waveOutClose(HWAVEOUT hwo)", "MMRESULT waveOutReset(HWAVEOUT hwo)"]),
            ("wave-out-pause", "waveOutPause", "waveOutRestart",
             ["MMRESULT waveOutPause(HWAVEOUT hwo)", "MMRESULT waveOutRestart(HWAVEOUT hwo)"]),
            ("wave-out-header", "waveOutPrepareHeader", "waveOutUnprepareHeader",
             ["MMRESULT waveOutPrepareHeader(HWAVEOUT hwo, LPVOID pwh, UINT cbwh)",
              "MMRESULT waveOutUnprepareHeader(HWAVEOUT hwo, LPVOID pwh, UINT cbwh)"]),
            ("wave-out-write", "waveOutWrite", "waveOutSetVolume",
             ["MMRESULT waveOutWrite(HWAVEOUT hwo, LPVOID pwh, UINT cbwh)",
              "MMRESULT waveOutSetVolume(HWAVEOUT hwo, DWORD dwVolume)"])]:
        row(id=rid, lib="winmm", members=[a, b], needs=["rt-core-time"], stage="D",
            header=None, core=[], model="AllocConsole", protos=protos,
            notes=PROPOSAL + NODEV + """\
- `waveOutOpen` never gives out a handle, so every handle is invalid: both functions `return
  MMSYSERR_INVALHANDLE;` ("Specified device handle is invalid", on each function's page), for
  every value, NULL included. A header pointer (`LPVOID` here: the page's `LPWAVEHDR`) is not
  read or written. Log line: `<Name>: there is no audio output device`.
- Tests: each with NULL and with `(HWAVEOUT)(uintptr_t)0x44`; a header of 48 bytes preset to
  0xA5 keeps them; last error is left alone; the log line.""")
    row(id="mixer-devs", lib="winmm", members=["mixerGetNumDevs", "mixerOpen"], needs=["rt-core-time"], stage="C",
        header=None, core=[], model="AllocConsole",
        protos=["UINT mixerGetNumDevs(void)", "MMRESULT mixerOpen(LPHMIXER phmx, UINT uMxId, DWORD_PTR dwCallback, "
                "DWORD_PTR dwInstance, DWORD fdwOpen)"],
        notes=PROPOSAL + NODEV + """\
- `mixerGetNumDevs`: `return 0;` ("Returns the number of mixer devices or zero if no mixer
  devices are available"). Status `full`, no log line: it is the true count.
- `mixerOpen`, from its page: when the object-type bits of `fdwOpen` (`fdwOpen &
  MIXER_OBJECTF_TYPEMASK`, `rt/win/mm.h`) are `MIXER_OBJECTF_MIXER`, `uMxId` is a mixer
  identifier and none is valid: `MMSYSERR_BADDEVICEID` ("The uMxId parameter specifies an
  invalid device identifier"). For any other object type: `MMSYSERR_NODRIVER` ("No mixer device
  is available for the object specified by uMxId"). `*phmx` is not written. Log line:
  `mixerOpen: there is no mixer device`.
- Tests: the count is 0, twice; `mixerOpen` with identifier 0 and flags 0, and with flags
  0x10000000 (a wave-out identifier); a preset `*phmx` keeps its value; last error is left
  alone; the log line.""")
    row(id="mixer-caps", lib="winmm", members=["mixerGetDevCapsA"], needs=["rt-core-time"], stage="C",
        header=None, core=[], model="AllocConsole",
        protos=["MMRESULT mixerGetDevCapsA(UINT_PTR uMxId, LPVOID pmxcaps, UINT cbmxcaps)"],
        notes=PROPOSAL + NODEV + """\
- `return MMSYSERR_BADDEVICEID;` ("The specified device identifier is out of range"): there
  are zero mixer devices, and no open mixer handle can be passed instead. `pmxcaps` (`LPVOID`
  here: the page's `LPMIXERCAPSA`) is not written. Log line: `mixerGetDevCapsA: there is no
  mixer device`.
- Tests: identifiers 0 and 5; a buffer of 80 bytes preset to 0xA5 keeps them; last error is
  left alone; the log line.""")
    row(id="play-sound", lib="winmm", members=["PlaySoundA"], needs=["rt-core-time"], stage="D",
        header=None, core=[], model="AllocConsole",
        protos=["BOOL PlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)"],
        notes=PROPOSAL + NODEV + """\
- `return FALSE;` for every call (the page: "Returns TRUE if successful or FALSE otherwise";
  nothing can be played, and a call that only stops a sound, `pszSound` NULL, has nothing to
  stop: that it is FALSE too is the planner's choice, not verified). `pszSound` is not read:
  with `SND_MEMORY` it is not a string. Log line, once: `PlaySoundA: there is no audio output
  device (flags 0x%X)`.
- Tests: a file name with flags 0x20001, NULL with flags 0; last error is left alone; the log
  line.""")

    # ---- ole32 ----------------------------------------------------------------------------------
    COM = """\
- There is no COM run time before Gate 3 (`docs/RUNTIME.md` 3.7 and 11.1). `GUID`, `REFCLSID`,
  `REFIID` and `LPUNKNOWN` are in `rt/win/com.h`, the `HRESULT` values in `rt/win/error.h`. These
  functions return an `HRESULT` and never touch last error.
"""
    row(id="co-create", lib="ole32", members=["CoCreateInstance"], needs=["rt-core-heap"], stage="C",
        header=None, core=[], model="AllocConsole",
        protos=["HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, "
                "LPVOID *ppv)"],
        notes=PROPOSAL + NOTHING_THERE + COM + """\
- `docs/RUNTIME.md` 3.7: "`CoCreateInstance` for a class we do not provide returns
  `REGDB_E_CLASSNOTREG` and logs the CLSID", and no class is provided. `ppv` NULL: `return
  E_POINTER;` (the page: "The ppv parameter is NULL"). Otherwise `*ppv = NULL` ("Upon failure,
  *ppv contains NULL"), log, `return REGDB_E_CLASSNOTREG;`. Whether the thread initialised COM
  is not looked at (Windows answers `CO_E_NOTINITIALIZED` first: Wine ole32 compobj.c
  test_CoCreateInstance).
- The log is not once per process but once per class: `rt_log_once(RT_CH_IMPORT, RT_LL_INFO, key,
  "CoCreateInstance: class {%08X-%04X-%04X-...} is not registered (interface {...})", ...)` with
  the 16 bytes of the CLSID as the key, printed in the registry's form (`rt/win/com.h` says
  which member goes where). `rclsid` or `riid` NULL is `RT_UNIMPLEMENTED`. Status `absent`,
  with that stop listed.
- Tests: a CLSID of the bytes 1 to 16: the result, `*ppv` NULL after a preset, and the log line
  with the CLSID's text; `ppv` NULL; last error is left alone; the NULL stops.""")
    row(id="co-task-mem", lib="ole32", members=["CoTaskMemFree"], needs=["rt-core-heap"], stage="E",
        header="rt/heap.h", core=["rt_heap_free", "rt_heap_process", "rt_galloc"], model="HeapFree",
        protos=["void CoTaskMemFree(LPVOID pv)"],
        notes=PROPOSAL + N.CORE_STOPS + """\
- `docs/RUNTIME.md` 5.4: memory the guest may hand to `CoTaskMemFree` comes from the process
  heap (`rt_galloc`). So: `if (pv) rt_heap_free(rt_heap_process(), 0, pv);` (the page: "If this
  parameter is NULL, the function has no effect"). Status `full`. No function of this runtime
  hands out such memory yet, so a pointer that is not NULL and not from `rt_galloc` is the
  core's stop, which is the wanted report.
- Tests: NULL; a block from `rt_galloc(32)` is freed (a second free is the stop `core: a
  pointer that is not a live block`); last error is left alone.""")
    APT = """\
- The core keeps, per thread, a count of initialisations, the concurrency model of the first
  and a count of OLE initialisations (`rt/thread.h`, "the COM state of a thread"); its comments
  are the specification, with the return values of the pages. A shim is one core call.
"""
    row(id="co-init", lib="ole32", members=["CoInitializeEx", "CoInitialize"],
        needs=["rt-contract-thread", "rt-core-small"], stage="A", header="rt/thread.h",
        core=["rt_com_initialize", "rt_com_uninitialize"], decl_only=["rt_com_uninitialize"],
        model="TlsGetValue",
        protos=["HRESULT CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit)", "HRESULT CoInitialize(LPVOID pvReserved)"],
        notes=PROPOSAL + COM + APT + N.CORE_STOPS + """\
- `CoInitializeEx`: `pvReserved` not NULL is `E_INVALIDARG` (the page: "This parameter is
  reserved and must be NULL"; the value returned is from memory); else `return
  rt_com_initialize(ctx, dwCoInit);`. The core stops on a flag outside the four `COINIT_` values
  (`rt/win/com.h`): `RT-TRAPS` `core: ...`, status `partial`.
- `CoInitialize(p)` is `CoInitializeEx(p, COINIT_APARTMENTTHREADED)` (its page): the same check
  of `pvReserved`, then `return rt_com_initialize(ctx, COINIT_APARTMENTTHREADED);`. It has no
  flag to stop on: status `full`. The launcher calls this before anything else of COM.
- Tests: `S_OK`, then `S_FALSE`, then `RPC_E_CHANGED_MODE` for the other model; a non-NULL
  `pvReserved`; a preset last error stays. A test ends with the thread uninitialised: call
  `rt_com_uninitialize(rt_test_ctx())` once for every `S_OK` and `S_FALSE` it got.""")
    row(id="co-uninit", lib="ole32", members=["CoUninitialize", "OleInitialize"],
        needs=["rt-contract-thread", "rt-core-small"], stage="A", header="rt/thread.h",
        core=["rt_com_uninitialize", "rt_ole_initialize", "rt_com_initialize"], decl_only=["rt_com_initialize"],
        model="TlsGetValue",
        protos=["void CoUninitialize(void)", "HRESULT OleInitialize(LPVOID pvReserved)"],
        notes=PROPOSAL + COM + APT + """\
- `CoUninitialize`: `rt_com_uninitialize(ctx);`. A call without an initialisation is logged by
  the core at warn level and does nothing (Windows does not report it). Status `full`.
- `OleInitialize(p)` (marked "tool path" in finding 24; `vgui2` has 3 call sites): `pvReserved`
  not NULL is `E_INVALIDARG` (the page: "This parameter is reserved and must be NULL"; the value
  is from memory); else `return rt_ole_initialize(ctx);`. The proposal read "as
  `CoInitializeEx(p, COINIT_APARTMENTTHREADED)`"; the core function differs from that in one
  result, which Wine's tests show for Windows: the FIRST `OleInitialize` of a thread is `S_OK`
  also when `CoInitializeEx` had run before. What OLE adds on top (clipboard, drag and drop)
  does not exist here: `RegisterDragDrop`, `RevokeDragDrop` and `ReleaseStgMedium` stay stops.
- Tests (set the state up with `rt_com_initialize(rt_test_ctx(), ...)`): `CoUninitialize` gives
  back one initialisation (after `S_OK` and `S_FALSE`, one call leaves the thread initialised:
  the other model is still `RPC_E_CHANGED_MODE`; a second call makes it `S_OK`); the warn line
  of a call too many (`T_LOGGED(RT_CH_THREAD, "no COM initialisation to give back")`).
  `OleInitialize`: `S_OK` then `S_FALSE`; `RPC_E_CHANGED_MODE` on a thread initialised with
  `COINIT_MULTITHREADED` (use one test order that reaches each); a non-NULL `pvReserved`; a
  preset last error stays.""")

    # ---- bcrypt and rpcrt4 ----------------------------------------------------------------------
    RND = """\
- The bytes come from `rt_random_bytes(buf, n)` (`rt/machine.h`), which cannot fail. They are
  not reproducible: a test checks what was written (exactly the bytes asked for; two calls
  differ), never values.
"""
    row(id="bcrypt-random", lib="bcrypt", members=["BCryptGenRandom"], needs=["rt-core-machine", "rt-core-small"], stage="C",
        header="rt/machine.h", core=["rt_random_bytes"], model="GetSystemTimeAsFileTime",
        protos=["NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE hAlgorithm, PBYTE pbBuffer, ULONG cbBuffer, "
                "ULONG dwFlags)"],
        notes=PROPOSAL + RND + """\
- From the page: with `dwFlags == BCRYPT_USE_SYSTEM_PREFERRED_RNG` (`rt/win/devices.h`)
  `hAlgorithm` must be NULL: fill the buffer and return `STATUS_SUCCESS`; with that flag and a
  handle, `STATUS_INVALID_PARAMETER`. Without the flag the handle must come from
  `BCryptOpenAlgorithmProvider`, which is a stop, so no handle is valid: `STATUS_INVALID_HANDLE`.
  That holds for the pre-defined handle 0x81 (`BCRYPT_RNG_ALG_HANDLE`) too: Windows 10 fills the
  buffer for it with flags 0, but the page "CNG Algorithm Pseudo-Handles" says those handles begin
  in Windows 10 and the machine is Windows 7 SP1 (`docs/RUNTIME.md` 8.5). Test it with 0x81.
  Any other flag bit is `RT_UNIMPLEMENTED`. `pbBuffer` NULL with a length: `STATUS_INVALID_PARAMETER`.
  The eight other `bcrypt` imports (the engine's AES) stay stops.""")
    row(id="uuid-create", lib="rpcrt4", members=["UuidCreate"], needs=["rt-core-machine", "rt-core-small"], stage="C",
        header="rt/machine.h", core=["rt_random_bytes"], model="GetSystemTimeAsFileTime",
        protos=["RPC_STATUS UuidCreate(UUID *Uuid)"],
        notes=PROPOSAL + RND + """\
- Sixteen random bytes with the version and variant of a random UUID: `Data3 = (Data3 & 0x0FFF) |
  0x4000`, `Data4[0] = (Data4[0] & 0x3F) | 0x80`; return `RPC_S_OK` (the page: the result "cannot
  be traced to the ethernet address of the computer"; that Windows makes version-4 UUIDs here:
  Wine rpcrt4 rpc.c test_UuidCreate). A NULL pointer is `RT_UNIMPLEMENTED`.""")
    row(id="uuid-string", lib="rpcrt4", members=["UuidToStringA", "RpcStringFreeA"], needs=["rt-core-heap"],
        stage="C", header="rt/heap.h", core=["rt_heap_alloc", "rt_heap_free", "rt_heap_process"], model="HeapAlloc",
        protos=["RPC_STATUS UuidToStringA(const UUID *Uuid, RPC_CSTR *StringUuid)",
                "RPC_STATUS RpcStringFreeA(RPC_CSTR *String)"],
        notes=PROPOSAL + N.CORE_STOPS + """\
- `UuidToStringA`: allocate 37 bytes with `rt_heap_alloc(rt_heap_process(), 0, 37)` (NULL:
  `return RPC_S_OUT_OF_MEMORY;`, the one failure on the page) and write the 36 characters
  `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` and a NUL: `Data1`, `Data2`, `Data3` as numbers, then
  `Data4[0..1]`, then `Data4[2..7]` (`rt/win/com.h`), in LOWER-case hexadecimal (from memory of
  Windows and of Wine's rpcrt4; the page does not say), digit by digit from a table: `stdio.h`
  is not available to a shim. Store the pointer in `*StringUuid`; `return RPC_S_OK;`. A NULL
  `Uuid` is the nil UUID, all zeros (from memory); a NULL `StringUuid` is `RT_UNIMPLEMENTED`.
- `RpcStringFreeA`: `if (*String) { rt_heap_free(rt_heap_process(), 0, *String); *String = NULL; }
  return RPC_S_OK;` (the only result on the page; that the pointer is set to NULL is from
  memory). A NULL `String` is `RT_UNIMPLEMENTED`. Neither function touches last error.
- Tests: the UUID with `Data1` 0x00112233, `Data2` 0x4455, `Data3` 0x6677 and `Data4` 88 99 AA
  BB CC DD EE FF gives `00112233-4455-6677-8899-aabbccddeeff`; NULL gives 32 zeros; the string
  is freed, the pointer becomes NULL and a second free returns `RPC_S_OK`; last error is left
  alone; the NULL stops.""")
    row(id="uuid-from-string", lib="rpcrt4", members=["UuidFromStringA"], needs=["rt-core-heap"], stage="C",
        header=None, core=[], model="GetTickCount",
        protos=["RPC_STATUS UuidFromStringA(RPC_CSTR StringUuid, UUID *Uuid)"],
        notes=PROPOSAL + """\
- The inverse of `UuidToStringA` (member layout in `rt/win/com.h`): exactly 36 characters,
  hyphens at positions 8, 13, 18 and 23, hexadecimal digits of either case elsewhere; anything
  else: `return RPC_S_INVALID_STRING_UUID;` with `*Uuid` untouched. A NULL string stores the nil
  UUID and returns `RPC_S_OK` (from memory of Wine's rpcrt4, not on the page). A NULL `Uuid` is
  `RT_UNIMPLEMENTED`. Status `partial`. Last error is never touched.
- Tests: `00112233-4455-6677-8899-aabbccddeeff` and the same in upper case give the members
  0x00112233, 0x4455, 0x6677, 88 99 AA BB CC DD EE FF; NULL gives 16 zero bytes; 35 characters,
  37 characters, a `g`, and a hyphen in the wrong place fail with a preset UUID untouched; last
  error is left alone; the NULL stop.""")

    # ---- shell32 and shlwapi ----------------------------------------------------------------------
    row(id="sh-folder-path", lib="shell32", members=["SHGetFolderPathA", "SHGetSpecialFolderPathA"],
        needs=["rt-core-path", "rt-core-small"], stage="B", header="rt/path.h", core=["rt_path_known_w", "rt_path_known_a"],
        decl_only=["rt_path_known_a"],
        model="GetCurrentDirectoryW",
        protos=["HRESULT SHGetFolderPathA(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath)",
                "BOOL SHGetSpecialFolderPathA(HWND hwnd, LPSTR pszPath, int csidl, BOOL fCreate)"],
        notes=PROPOSAL + """\
- A shim may not spell a path: the text comes from `rt_path_known_a(which, pszPath, MAX_PATH)`
  (the comment above the W form is the specification of both; `which` is a value of the enum
  `RtKnownPath` of `rt/path.h`). `pszPath` is a buffer of `MAX_PATH` bytes (the page), and every
  text fits in it, so the call always writes the text. A NULL `pszPath` is `RT_UNIMPLEMENTED`
  in the shim, before the core call.
- The folders exist in the guest's tree (`docs/RUNTIME.md` 7.1), so the answers are real. With
  `csidl & ~CSIDL_FLAG_CREATE` (the constants are in `rt/win/shell.h`; `CSIDL_FLAG_CREATE` is
  accepted, the folders exist): `CSIDL_PERSONAL` is `RT_KNOWN_DOCUMENTS`
  (`C:\\Users\\Player\\Documents`), `CSIDL_LOCAL_APPDATA` `RT_KNOWN_LOCAL_APPDATA`
  (`C:\\Users\\Player\\AppData\\Local`), `CSIDL_APPDATA` `RT_KNOWN_APPDATA`
  (`C:\\Users\\Player\\AppData\\Roaming`), `CSIDL_PROFILE` `RT_KNOWN_PROFILE`
  (`C:\\Users\\Player`), `CSIDL_WINDOWS` `RT_KNOWN_WINDOWS` (`C:\\Windows`), `CSIDL_SYSTEM`
  `RT_KNOWN_SYSTEM` (`C:\\Windows\\system32`); none ends in a backslash (the page). Any other
  folder is `RT_UNIMPLEMENTED` with the number: the first run that asks says which. In
  `SHGetFolderPathA`, `hToken` not NULL and `dwFlags` other than `SHGFP_TYPE_CURRENT` or
  `SHGFP_TYPE_DEFAULT` are stops too. `SHGetFolderPathA` returns `S_OK`,
  `SHGetSpecialFolderPathA` TRUE (its `fCreate` is ignored: the folders exist).
- Status `partial`. Tests, per function: each of the six folders gives its text (compare bytes
  with a `char` array the test spells; a test may spell a path); `CSIDL_PERSONAL |
  CSIDL_FLAG_CREATE` gives the Documents text; bytes of a preset buffer after the terminator
  stay; a preset last error stays; one `T_TRAPS` per stop (`CSIDL_DESKTOP` for "another folder").""")
    row(id="shell-execute", lib="shell32", members=["ShellExecuteA"], needs=["rt-contract-file"], stage="A",
        header=None, core=[], model="SuspendThread",
        protos=["HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, "
                "LPCSTR lpDirectory, INT nShowCmd)"],
        notes=PROPOSAL + TRAP + """\
- "Child processes (`CreateProcess`, pipes to them, `ShellExecute`) are a feature this machine
  does not have, decided when a caller appears" (`docs/RUNTIME.md` 7.3). The stop says what was
  to be opened: `RT_UNIMPLEMENTED(ctx, "ShellExecuteA(operation \\"%s\\", file \\"%s\\", parameters
  \\"%s\\")", ...)` with `"(null)"` for a NULL string.
- Test: `T_TRAPS(CALL(shell32, ShellExecuteA, NULL, "open", "http://example.invalid/", NULL, NULL,
  1), "ShellExecuteA(operation \\"open\\", file \\"http://example.invalid/\\", parameters
  \\"(null)\\")");`.""")
    row(id="sh-delete-key", lib="shlwapi", members=["SHDeleteKeyA"], needs=["rt-core-registry", "rt-core-small"],
        stage="D", header="rt/registry.h",
        core=["rt_reg_delete_tree_w", "rt_reg_delete_tree_a", "rt_reg_create_key_ex_a", "rt_reg_open_key_ex_a",
              "rt_reg_close_key"],
        decl_only=["rt_reg_create_key_ex_a", "rt_reg_open_key_ex_a", "rt_reg_close_key"], model="CloseHandle",
        protos=["LSTATUS SHDeleteKeyA(HKEY hkey, LPCSTR pszSubKey)"],
        notes=PROPOSAL + REG + N.CORE_STOPS + """\
- `rt_reg_delete_key_ex_a` refuses a key that has subkeys, and this function "deletes a subkey
  and all its descendants": one core call, `return rt_reg_delete_tree_a(hkey, pszSubKey);` (the
  comment above the W form is the specification of both). The core stops on a NULL or empty
  subkey and on a seeded key in the tree: `RT-TRAPS` `core: ...`, status `partial`.
- Tests: make `Software\\RtShimTest\\Top\\A` and `...\\Top\\B` under `HKEY_CURRENT_USER`; deleting
  `Software\\RtShimTest\\Top` is `ERROR_SUCCESS` and opening it afterwards `ERROR_FILE_NOT_FOUND`;
  deleting it again is `ERROR_FILE_NOT_FOUND`; a handle kept open to `...\\Top\\A` is closed with
  `ERROR_SUCCESS` afterwards; a preset last error (0x1234) stays in the calls on
  `HKEY_CURRENT_USER`; the stops: a NULL subkey, an empty one, and `HKEY_LOCAL_MACHINE` with
  `"HARDWARE"` (seeded).""")

    # ---- setupapi, hid, avrt, iphlpapi ------------------------------------------------------------
    NOHID = NOTHING_THERE + """\
- What is not there: a HID device (`docs/RUNTIME.md` 9.2 names "no HID devices" as an `absent`
  answer). Windows answers a query for devices that do not exist with an EMPTY device
  information set, not with a failure (Wine setupapi devinst.c test_get_class_devs; the page
  of `SetupDiEnumDeviceInterfaces` describes the loop that ends with `ERROR_NO_MORE_ITEMS`). The one set this runtime has is the
  constant `RT_HDEVINFO_EMPTY` of `rt/win/devices.h`: it is not a kernel handle and holds nothing.
"""
    row(id="hid-guid", lib="hid", members=["HidD_GetHidGuid"], needs=["rt-contract-thread"], stage="D",
        header=None, core=[], model="GetTickCount",
        protos=["void HidD_GetHidGuid(LPGUID HidGuid)"],
        notes=PROPOSAL + """\
- A real answer: store the device interface class of HID collections, `GUID g =
  RT_GUID_DEVINTERFACE_HID; *HidGuid = g;` (`rt/win/devices.h`: {4D1E55B2-F16F-11CF-88CB-
  001111000030}: the SDK's `GUID_DEVINTERFACE_HID` in Microsoft's Win32 metadata). Status
  `full`. A NULL pointer is `RT_UNIMPLEMENTED` (then `partial`).
- Tests: the 16 bytes are B2 55 1E 4D 6F F1 CF 11 88 CB 00 11 11 00 00 30; last error is left
  alone; the NULL stop.""")
    row(id="setupdi-set", lib="setupapi", members=["SetupDiGetClassDevsA", "SetupDiDestroyDeviceInfoList"],
        needs=["rt-contract-thread"], stage="D", header=None, core=[], model="AllocConsole",
        protos=["HDEVINFO SetupDiGetClassDevsA(LPCGUID ClassGuid, LPCSTR Enumerator, HWND hwndParent, DWORD Flags)",
                "BOOL SetupDiDestroyDeviceInfoList(HDEVINFO DeviceInfoSet)"],
        notes=PROPOSAL + NOHID + """\
- `SetupDiGetClassDevsA`: `Flags` with a bit outside `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE`,
  or without `DIGCF_DEVICEINTERFACE`, or `Enumerator` not NULL, or `ClassGuid` NULL, is
  `RT_UNIMPLEMENTED` with the values (only the query for present interfaces of one class is
  answered); otherwise log `SetupDiGetClassDevsA: no device of class {...}` once and `return
  RT_HDEVINFO_EMPTY;`. Last error is left alone.
- `SetupDiDestroyDeviceInfoList`: `RT_HDEVINFO_EMPTY` returns TRUE; any other value FALSE with
  `ERROR_INVALID_HANDLE`. No log line.
- Tests: the query with the HID class GUID and flags 0x12 returns the constant, twice; destroy
  returns TRUE; destroy of NULL and of `INVALID_HANDLE_VALUE` fails with the code; the stops
  for flags 0x02 and for an enumerator.""")
    row(id="setupdi-enum", lib="setupapi", members=["SetupDiEnumDeviceInterfaces"], needs=["rt-contract-thread"],
        stage="D", header=None, core=[], model="AllocConsole",
        protos=["BOOL SetupDiEnumDeviceInterfaces(HDEVINFO DeviceInfoSet, LPVOID DeviceInfoData, "
                "LPCGUID InterfaceClassGuid, DWORD MemberIndex, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData)"],
        notes=PROPOSAL + NOHID + """\
- `DeviceInfoSet == RT_HDEVINFO_EMPTY`: `return FAIL(ctx, ERROR_NO_MORE_ITEMS, FALSE);` for every
  index (the page: the caller increments `MemberIndex` "until this function fails and
  GetLastError returns ERROR_NO_MORE_ITEMS"). Any other set: FALSE with `ERROR_INVALID_HANDLE`.
  `*DeviceInterfaceData` is not written (`DeviceInfoData` is `LPVOID` here: the page's
  `PSP_DEVINFO_DATA`, never read). No log line: the loop's end is its normal result.
  `SetupDiGetDeviceInterfaceDetailA` and the four other `hid` functions need an interface or an
  open device, which cannot exist: they stay stops.
- Tests: index 0 and index 3 on the empty set; a structure preset to 0xA5 keeps its 32 bytes;
  a set of NULL (`ERROR_INVALID_HANDLE`).""")
    row(id="avrt-mm-thread", lib="avrt", members=["AvSetMmThreadCharacteristicsW", "AvRevertMmThreadCharacteristics"],
        needs=["rt-contract-thread"], stage="E", header=None, core=[], model="GetTickCount",
        protos=["HANDLE AvSetMmThreadCharacteristicsW(LPCWSTR TaskName, LPDWORD TaskIndex)",
                "BOOL AvRevertMmThreadCharacteristics(HANDLE AvrtHandle)"],
        notes=PROPOSAL + ADVISORY + """\
- `docs/RUNTIME.md` 6.3: "the two `avrt` calls succeed": they ask the scheduler to favour an
  audio thread, and guest threads already run at the highest class the host gives. The handle
  is the constant `RT_AVRT_TASK` of `rt/win/mm.h` (not a kernel handle, as on Windows).
- `AvSetMmThreadCharacteristicsW`, from its page: `*TaskIndex` 0 ("must be 0 on input" the
  first time): store 1 and return `RT_AVRT_TASK`; `*TaskIndex` 1 (the value given out): return
  `RT_AVRT_TASK`; any other value: NULL with `ERROR_INVALID_TASK_INDEX`. The task name is not
  looked up (`ERROR_INVALID_TASK_NAME` would need the registry's task list: not modelled). A
  NULL `TaskName` or `TaskIndex` is `RT_UNIMPLEMENTED`.
- `AvRevertMmThreadCharacteristics`: `RT_AVRT_TASK` returns TRUE; any other value FALSE with
  `ERROR_INVALID_HANDLE` (the code is from memory). No log line of its own.
- Tests: index 0 becomes 1 with the handle; index 1 stays; index 7 fails with the code; revert
  of the handle and of NULL; success leaves last error alone; the log line; the NULL stops.""")
    return out
