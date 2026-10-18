# Writing one shim

The runtime stands in for Windows: lifted game code calls an imported function, the call
goes through an import slot and the dispatch table to a *shim*, a host C function you
write. The contract is `docs/RUNTIME.md`; read its sections 3, 9 and 10, then the part of
sections 4 to 8 that covers your function. This page is the short version.

## You edit exactly two files

    runtime/shims/<lib>/<Name>.c          the shim
    runtime/tests/shims/<lib>/<Name>.c    its test

`<lib>` is the DLL name in lower case without `.dll`, other punctuation as `_` (`kernel32`,
`ws2_32`). `<Name>` is the export's exact spelling. Nothing registers a shim: it is built
and tested because the file exists. Copy `kernel32/GetLastError.c` (both files) to start;
`runtime/tests/abi/rttest/` has examples of floats, structs, callbacks and a `partial` shim.

**If you need anything that is not there, stop and say so in the issue.** A type that is
not in `runtime/tools/types.def`, a struct or constant that is not under
`runtime/include/rt/win/`, a core function that does not exist or that aborts with
"unimplemented runtime core function", a library that is not in `runtime/modules.def`, a
function that is not in `runtime/tools/imports.def`: none of these is yours to add. Never
edit a shared file, and never work around a missing piece inside your shim.

## The shim

```c
/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-readfile
 * RT-CASES:  synchronous read from a file; zero-length read; end of file
 * RT-TRAPS:  OVERLAPPED
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, ReadFile, HANDLE hFile, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov)
{
    if (ov) RT_UNIMPLEMENTED(ctx, "ReadFile with OVERLAPPED %p", (void *)ov);
    ...
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
```

- **The four tags** start the file. `RT-STATUS` is one of the rows below. `RT-DOC` is the
  Microsoft documentation page. `RT-CASES` lists what the shim handles and `RT-TRAPS` what
  it refuses, as entries separated by `;` (`none` if there are none).

  | Status | Meaning | Rule |
  |---|---|---|
  | `full` | documented behaviour, failure returns included | no `RT_UNIMPLEMENTED` |
  | `partial` | the listed cases work | every other case calls `RT_UNIMPLEMENTED` with the offending value, and is listed in `RT-TRAPS` |
  | `absent` | the machine does not have the feature | returns the failure Windows documents, and logs; planner's sign-off in the issue |
  | `advisory` | Windows documents the call as a hint | may succeed without effect; logs once at debug level; planner's sign-off |
  | `trap` | deliberately not implemented | the body is `RT_UNIMPLEMENTED` |

  There is no status for "returns success and does nothing". Never return a default for a
  case you did not implement.
- **The `SHIM` line is one line**, starting in column 1. Copy the prototype from the
  documentation; `ctx` is in scope in the body. No parameters: `SHIM(kernel32, DWORD, Name)`.
  `long`, `unsigned long`, `wchar_t` and `size_t` are forbidden (use `LONG`, `WCHAR`,
  `SIZE_T`). Use `SHIM_ORD(lib, ordinal, ret, Name, ...)` for an import by ordinal (file
  `ord<number>_<Name>.c`) and `SHIM_RAW(lib, Name)` only for varargs.
- **Arguments** arrive typed; the generated thunk does the marshalling. A guest pointer is an
  ordinary C pointer. A `GUESTFN` is a guest code address: call it with
  `GUEST_CALL(ctx, fn, a, b)`, never as a C function.
- **Last error**: set it on failure (`return FAIL(ctx, ERROR_X, value);`), leave it alone on
  success, unless the documentation says otherwise. Map a host `errno` only with
  `rt_win32_from_errno(errno, RT_ERR_FILE)`. When the failure comes from a core function,
  who sets it depends on the header; setting it twice or not at all is the usual mistake:

  | Core header | The core function | Your shim |
  |---|---|---|
  | `handle.h`, `sync.h`, `vm.h`, `locale.h`, `module.h`, `thread.h`, `lock.h`, `crash.h`, `window.h` | sets last error itself when it fails, and leaves it alone on success | returns the result and sets nothing |
  | `heap.h` | `rt_heap_create`, `rt_heap_destroy` and `rt_heap_alloc` (which sets `ERROR_NOT_ENOUGH_MEMORY` when it returns NULL) as the row above; the others never set it (`heap.h`, LAST ERROR) | returns the result and sets nothing |
  | `file.h` | as the first row, with three functions that set it on success too: `rt_file_create` and `rt_section_create` (`ERROR_ALREADY_EXISTS` or 0), and `rt_file_read_ex` (0, as `ReadFileEx` is documented to) | returns the result and sets nothing |
  | `gdi.h` | sets last error only where the function's comment names a code (`ERROR_INVALID_HANDLE` for a handle that is not a device context), and leaves it alone otherwise (`gdi.h`, LAST ERROR) | returns the result and sets nothing |
  | `net.h` | does all of it (`WSAGetLastError` reads the same value): sets the code when it fails; sets 0 when it succeeds in the ten functions its header lists and leaves it alone on the other successes (`net.h`, LAST ERROR); `rt_net_startup` returns its code and never touches it | returns the result and sets nothing |
  | `env.h` | never touches last error; gives the code through its last argument, `DWORD *err` (0: none) | `if (err) return FAIL(ctx, err, value);` |
  | `path.h` | never touches last error; **returns** the code (0: success) | `if (e) return FAIL(ctx, e, value);` |
  | `registry.h` | never touches last error; returns the status that the `Reg*` function itself returns | `return rt_reg_...;` and sets nothing |
  | `time.h`, `machine.h` | cannot fail | sets nothing |
  | `unwind.h` | never reads or writes last error (`unwind.h`, LAST ERROR) | sets nothing |

  Three exceptions, each stated in its header: `rt_event_create`, `rt_mutex_create` and
  `rt_semaphore_create` set last error on success too (`ERROR_ALREADY_EXISTS` or 0), and
  `rt_tls_get` and `rt_fls_get` set `ERROR_SUCCESS` when they succeed (`rt_tls_get` as
  `TlsGetValue` documents).
- **Shared logic lives in `runtime/core`**, behind the headers in `runtime/include/rt/`
  (`handle.h`, `sync.h`, `vm.h`, `heap.h`, `lock.h`, `thread.h`, `module.h`, `file.h`, `path.h`,
  `registry.h`, `env.h`, `locale.h`, `time.h`, `machine.h`, `crash.h`, `unwind.h`, `net.h`,
  `window.h`, `gdi.h`, `log.h`). A shim does not call another shim, does not call `malloc`, and does not
  touch a path string itself (`rt_path_to_host_*` does). (`com.h` and `nulldev.h` are the
  interface layer and the do-nothing Direct3D device: a method of an interface is not a shim,
  and `docs/handoff/d3d-interface-layer.md`, section 2, is the page for writing one.)
- **Logging**: `rt_log(RT_CH_FILE, RT_LL_INFO, "...")`, `rt_log_once(channel, level, key, "...")`.

## The test

```c
#include "rt/test.h"

TEST(kernel32, ReadFile)
{
    T_CASE("zero-length read");                       /* the RT-CASES entry, spelled the same */
    T_EQ(CALL(kernel32, ReadFile, h, buf, 0, &got, NULL), TRUE);
    T_EQ(got, 0);
    T_TRAPS(CALL(kernel32, ReadFile, h, buf, 1, &got, &ov), "ReadFile with OVERLAPPED");
}
```

`CALL` runs the shim through the guest ABI (import slot, dispatch table, generated thunk)
with garbage in every undefined bit and checks on every register the shim must preserve.
One `T_CASE` per `RT-CASES` entry, each with at least one check; every `ERROR_*` code the
shim sets is checked with `T_EQ(T_LAST_ERROR(), ERROR_X)`. `T_TRAPS(statement, "text")`
expects a fatal report. Each test runs in its own process with a temporary state directory
and install root (`rt_test_state_dir()`, `rt_test_install_root()`); it never sees the real
install. All macros are documented in `runtime/include/rt/test.h`.

A shim test starts no threads: `CALL`, `T_CASE`, `T_LAST_ERROR` and `T_TRAPS` belong to the
test's main thread. (In a test of the core the checks `T_TRUE` and `T_EQ` may be used on other
threads, within the limits listed under THREADS in `rt/test.h`; that has not been shown safe
for shim tests.) A test gets 300 seconds (`RT_TEST_TIMEOUT=<seconds>` changes it); a test
that has not ended by then is killed with every process it forked and reported as
`timed out after N s`, with the case it was in and the last lines it logged.

## Running

    make rt-test SHIM=kernel32/ReadFile                    your test
    make rt-test SHIM=kernel32/ReadFile RT_TEST_ARGS=-v    with cases, log lines and fatal reports
    RT_LOG=trace make rt-test SHIM=kernel32/ReadFile       with one line per shim call and return
    make rt-test                                           every test
    RT_TEST_TIMEOUT=30 make rt-test SHIM=kernel32/ReadFile  with a time limit of 30 s instead of 300
    make rt-test-tsan                                      every test, under ThreadSanitizer (build/runtime-tsan)
    make rt-test-tsan SHIM=core/lock_cs_stress             one test under it

`make rt-test-tsan` builds the suite with ThreadSanitizer into `build/runtime-tsan` and runs
it. A test in which the sanitizer reports a data race fails (its process ends with status 66),
so memory that one thread of a test writes while another reads needs atomics on both sides.
`runtime/tests/tsan.skip` lists the tests that cannot work under the sanitizer, each with its
reason (the runner's skip list: `RT_TEST_SKIP=<file>`, a line is `lib/Name  reason`).
`runtime/tests/tsan.supp` is for reports in `runtime/core/` that are known and not yet fixed;
it is empty.

## Done

    make shim-check SHIM=kernel32/ReadFile

must exit 0. It checks (RUNTIME.md section 10.5): (1) only your two files changed since the
branch point; (2) file name, `SHIM` line and tags agree and the function is in the import
list; (3) lint: forbidden types, direct allocation, path handling, `errno` mapping,
includes outside the allowed list, calls to other shims; (4) the runtime builds with no
warnings; (5) your test passes through the guest ABI and calls your shim; (6) every
`RT-CASES` entry has a case that ran and checked something, and every error code the shim
names is tested; (7) every `RT-TRAPS` entry is reached by an `RT_UNIMPLEMENTED` call, or is
written `core: <case>` for a stop the core makes itself (its header comment says "fatal"):
add no guard for such a case, list it, and prove it with `T_TRAPS`;
(8) the whole test suite still passes. Commit your two files (and nothing else) before you
report; say in the issue anything you were unsure of.
