# Runtime design (v0)

The native runtime that lifted game code is linked against: loader, dispatch, Windows API
shims, threads, memory, files. This document is the contract for everyone who writes runtime
code, in the way `docs/DESIGN.md` is the contract for the lifter. Change it deliberately, in
its own commit.

Status: design only. Nothing here is implemented.

Terms are those of `docs/DESIGN.md`: *guest* is the original x86-64 Windows code and its view
of the world, *host* is the arm64 macOS process, *module* is one game DLL, *RVA* is an offset
from a module's base. New here: a *shim* is a host C function that stands in for one imported
Windows function; a *native module* is a system DLL (or a game DLL we chose not to lift) that
exists only as shims.

**If you are writing one shim, read sections 3, 9 and 10, then the part of sections
4 to 8 that covers your function.**

Each decision below has a reason and, in one line, what was rejected. A decision marked
**(E*n*)** depends on an experiment listed in section 11.

## 0. Facts this design was fitted to

Measured on 2026-10-08 with the project's loader and import-survey scripts, in addition to the earlier plan's record.

| Fact | Value |
|---|---|
| External imports | 739 in the 24 DLLs; 192 in tier0 + vstdlib; 556 once unshipped, stubbed and OriginSDK-only ones are set aside |
| Image size (`SizeOfImage`) | About 520 MB for the 24 DLLs, 350 MB of it `engine.dll` (mostly zero-filled data) |
| Section alignment | 4 KB in every DLL, so sections with different protections share 16 KB host pages |
| TLS directories | 17 of the 24 DLLs (the 16 Respawn modules and OriginSDK), not all 24. Templates are 4,128 to 4,145 bytes (24 in OriginSDK), no zero fill, **no TLS callbacks anywhere** |
| Names looked up at run time | The C runtime in 17 DLLs asks `GetProcAddress` for 29 newer kernel32 functions at start-up and falls back when told NULL. Other DLLs name `xinput1_3.dll`, `xaudio2_7.dll`, `dsound.dll`, `ntdll.dll` and more |
| Game allocator | vstdlib references tier0's allocator object at 995 sites and `HeapAlloc` at 1. tier0 has 4 `HeapAlloc` sites and 11 `VirtualAlloc` sites |
| Not imported at all | `VirtualProtect`, fibers, waitable timers, I/O completion ports, `RtlRestoreContext`, vectored exception handlers |
| Threads | No fibers. Worker count is derived from `GetLogicalProcessorInformation`. 3,371 critical-section sites, 525 SRW-lock sites, 434 `SList` sites in the Respawn modules |
| Stack limit reads | Exactly one `mov r11, gs:[0x10]` in each of the 24 DLLs: the C runtime's stack probe |
| Thread-naming exception | The code `0x406D1388` is present in the Miles, Bink, audition and OriginSDK DLLs and in no Respawn module |
| Host | A test machine (M3 Pro): 5 performance and 6 efficiency cores, 16 KB pages, 18 GB |
| Host addresses | A throwaway probe run as a plain command-line program (one run, not kept): both reservations of section 1.2 succeeded where asked; default and non-default malloc zones returned addresses between 4 GB and 0.5 TB |

## 1. Process and image model

### 1.1 One host program, images mapped as data

**Decision.** The lifted C of every module is linked statically into one host executable
inside the app bundle. At run time our loader reads each module's original DLL file and
builds its image in guest memory: headers and every section, **including `.text`**, at
`base + RVA`. The image is never executable. A function's guest address is `base + RVA` of
the original function, exactly as `docs/DESIGN.md` section 5 already assumes for return
addresses.

**Why.** Guest data is full of code addresses (263,882 relocated slots point into code), and
guest code compares them, stores them, walks its own unwind and export tables and reads
constants that sit in `.text`. Mapping the whole original image keeps every such address
real, unique and readable, and it gives each function a stable identity for the dispatch
table. Never executing it means a dispatch bug cannot silently run x86 bytes.

*Rejected:* mapping only data sections (code addresses would dangle); `dlopen` of one dylib
per module (no benefit, and it loses fixed addresses).

### 1.2 Fixed addresses in two reserved arenas, all below 8 TB **(E2)**

**Decision.** The first thing `main` does is reserve two ranges with `PROT_NONE`:

| Arena | Proposed address | Size | Holds |
|---|---|---|---|
| Image arena | `0x0000_0100_0000_0000` | 4 GiB | Lifted modules in manifest order, then native modules; from `0x0000_0100_8000_0000` on, the lifted modules whose row says `place=late` |
| VM arena | `0x0000_0110_0000_0000` | 256 GiB | `VirtualAlloc`, file views, guest stacks, thread blocks, TLS blocks |

Every module gets its base at start-up, in manifest order, 64 KB aligned with a 64 KB gap,
whether or not it is ever loaded. Bases are therefore the same in every run of a build.
A lifted module added to a list whose addresses are in use is written `place=late` and gets
its base from the second half of the arena (`RT_IMAGE_LATE_BASE`, `rt/module.h`; added
for the game's `ui(11).dll`): otherwise a new lifted row would move the base
of every native module, since those follow the lifted ones.
Memory whose address the guest will see never comes from plain `malloc`; it comes from the
arenas or from the process-heap zone (section 5.4).

**Why.** Deterministic addresses make two runs comparable and make a log line such as
`tier0+0x1a40` reproducible. Staying below 8 TB lets section 5.1 report the Windows 7
address limit truthfully, and is a precaution against code that packs pointers into fewer
bits than a pointer has; no such code is known. Reserving both arenas before any framework
starts keeps later host mappings out of them. The probe in section 0 says the addresses are
available in a bare process; E2 repeats it in the real one.

*Rejected:* letting `mmap` choose (addresses change per run); the harness's
`0x2000_0000_0000` region (above 8 TB); the preferred base `0x180000000` (inside the macOS
shared cache).

### 1.3 Three kinds of module, one manifest

**Decision.** `runtime/modules.def` (core-owned) lists every module the process can load:

| Kind | Example | Image | Exports come from |
|---|---|---|---|
| `lifted` | `tier0.dll` | Built from the DLL file | The image's own export directory |
| `system` | `kernel32.dll` | Native module: a small synthetic image | The shim table |
| `stubbed` | `GFSDK_SSAO.win64.dll`, `OriginSDK.dll` until its shim exists | Native module | The shim table; a missing export is a trap stub |

A native module occupies a 1 MiB slot: a minimal valid PE header with no export directory
at `+0`, 16-byte shim stubs from `+0x10000`, guest-visible vtables for native objects from
`+0x80000` (section 3.7). Its `HMODULE` is its base and `GetModuleFileName` answers
`C:\Windows\system32\<name>`. There is also a native module for the executable, so
`GetModuleHandle(NULL)` and `GetModuleFileName(NULL)` (`C:\Titanfall2\Titanfall2.exe`) work
without ever touching the wrapped launcher.

The build bakes into the manifest, for each lifted module, its file size, PE timestamp,
`SizeOfImage` and SHA-256. **The loader refuses a DLL whose identity does not match the
lifted code it is about to pair with.**

**Why.** One registry answers every "which module is this" question (`LoadLibrary`,
`GetModuleHandle`, `RtlPcToFileHeader`, `VirtualQuery`, our own crash reports). Giving
system DLLs a real address range means a shim's address is an ordinary guest code address:
it can sit in an import slot, be stored, compared and returned by `GetProcAddress` like any
other. The identity check exists because lifted C from one build of a DLL run against
another build fails in ways that look like lifter bugs.

*Rejected:* using the host function's own address as the shim's guest address (host
pointers leak into guest data, differ per build, and no module owns them); a full fake
export directory for system DLLs (deferred until something is seen walking one by hand).

### 1.4 What goes in an import slot

**Decision.** The loader ignores whatever the file has in its import address table and
fills every slot from the import name table:

| The import resolves to | Slot value | A call through the slot reaches |
|---|---|---|
| A function exported by a lifted module | `base + export RVA` | That lifted function, through the dispatch table |
| Data exported by a lifted module | `base + export RVA` | Nothing: the guest reads the data |
| A shim in a native module | That module's stub address for the shim | The shim's generated thunk, through the dispatch table |
| Nothing we implement | A trap stub created for that (library, name) | `rt_trap_unimplemented`, which names the import and the caller (section 9) |

`call [rip+slot]` is already lifted as `CALL_INDIRECT(LD64(BASE + slot))`, so nothing in
the lifter changes. An import from a lifted module whose export does not exist is a fatal
load error (the build is inconsistent). Forwarded exports stop the load with a message; none
are expected. Delay-load tables are left alone: the C runtime's delay-load helper inside the
module calls our `LoadLibrary` and `GetProcAddress` by itself.

**Why.** A slot has to hold a genuine guest address because the guest does more than call
through it: tier0 exports data that vstdlib reads through its slots, and compilers load a
slot into a register and keep it. A unique trap stub per missing import lets a module with
unfinished imports load and run until it actually needs one, and then says which.

*Rejected:* slot holds a host function pointer or an index (breaks data imports and stored
pointers); refusing to load until every import exists (nothing could be tested for weeks).

### 1.5 `LoadLibrary`, `GetModuleHandle`, `GetProcAddress`

**Decision.**

- Name handling: take the last path component, lower-case it, append `.dll` if it has no
  extension. Look it up among loaded modules, then in the manifest.
- A manifest module loads (section 1.6). A name not in the manifest fails with
  `ERROR_MOD_NOT_FOUND` and one log line per call. This is what Windows does for a DLL that
  is not installed, and callers handle it.
- `LOAD_LIBRARY_AS_DATAFILE` and the other mapping-only flags trap until a caller appears.
- Modules are **never unloaded**. `FreeLibrary` decrements a count, returns TRUE, and logs
  when a count reaches zero.
- `GetModuleHandle(NULL)` is the executable's native module. `GetModuleHandleEx` with
  `FROM_ADDRESS` and `RtlPcToFileHeader` search module ranges, native modules included.
- `GetProcAddress` on a lifted module searches the mapped export directory, by name or by
  ordinal, and returns `base + RVA` for functions and data alike. On a native module it
  searches the shim table. **A name we do not have returns NULL with
  `ERROR_PROC_NOT_FOUND`, logged once per (module, name); it does not trap.**
- **The exception: a stubbed module** (a game DLL that was not lifted
  and not read: `OriginSDK.dll`, the two GFSDK libraries, `d3dcompiler_46.dll`). There a
  look-up by name or ordinal that finds no shim **stops the run** with `FATAL unimplemented
  import GetProcAddress(<module>, <name>)` and the caller; it is never answered NULL.
  Whether such a DLL exports a name is not known here; for EA's kit every function without a
  stand-in must stop the run, so a look-up there never answers NULL (`rt_module_lookup_stops`,
  `rt/module.h`).
  System modules and the executable answer NULL as before: the C runtime's probes need it.
- Families are exposed whole or not at all: the four `Fls*` functions, the thread-pool
  functions and each `*Ex` locale group land in one change (section 10.5).

**Why.** Probing for optional functions is normal: the C runtime in 17 DLLs asks for 29
functions at start-up and falls back to an older import for each NULL. Trapping there would
stop every DLL at its first instruction; answering NULL is correct Windows behaviour for an
older system and shrinks the surface. The family rule exists because that fallback is per
function: `FlsAlloc` present with `FlsGetValue` absent would mix two index spaces. Never
unloading is safe for a process that loads its DLLs once, and it keeps the dispatch table
and every code address valid for the life of the process.

*Rejected:* trapping on unknown `GetProcAddress` names in a system module; real unloading (would need to
restore pristine data sections and retire dispatch entries).

### 1.6 Load order, entry points, TLS

**Decision.** Loading module X, under one recursive loader lock:

1. Verify the file, build the image at X's base, apply the DIR64 relocations with
   `delta = base - ImageBase`, set `g_base_X`. A module with no relocation directory
   (the `ui*.dll` files) is simply mapped.
2. Register the image with the VM map as `MEM_IMAGE` (section 5.2).
3. For each import descriptor, load that module completely first (steps 1 to 7), then fill
   the slots. A module already being loaded higher up the recursion counts as loaded.
4. If X has a TLS directory: write its index (assigned statically, in manifest order) to
   the address the directory names, and give every existing guest thread a copy of X's TLS
   template.
5. Protect host pages that hold only read-only sections. Pages shared with a writable
   section stay writable.
6. Call X's TLS callbacks with `DLL_PROCESS_ATTACH` (none exist today), then its entry
   point as `DllMain(base, DLL_PROCESS_ATTACH, NULL)`.
7. If the entry point returns FALSE, `LoadLibrary` fails with `ERROR_DLL_INIT_FAILED` and
   the failure is logged. The image stays mapped.

Thread start runs, on the new thread and under the loader lock, TLS callbacks then
`DllMain(DLL_THREAD_ATTACH)` for every loaded module in load order. Thread exit runs
`DLL_THREAD_DETACH` in reverse order. `DLL_PROCESS_DETACH` is not delivered until Gate 5
(section 11).

**Why.** This is the Windows order, and the static C runtime depends on it: each DLL's
entry point is the C runtime start-up, which needs its dependencies initialised, and its
thread-attach call sets up per-thread state. Assigning TLS indices statically lets every
thread's TLS pointer array be allocated full-size at thread creation, so loading a module
later never has to move an array another thread is reading.

*Rejected:* lazy TLS blocks allocated on first access (the access is an inlined
`mov rax, gs:[0x58]` with no hook).

## 2. Dispatch: guest code address to host function

**Decision.** One global open-addressing hash table, keyed by guest address, built once in
`rt_init` and then made read-only.

```c
typedef struct { uint64_t guest; LiftedFn fn; } RtDispEntry;      /* 16 bytes */

static inline LiftedFn rt_lookup(Ctx *ctx, uint64_t target) {
    uint64_t i = (target * UINT64_C(0x9E3779B97F4A7C15)) >> g_disp_shift;
    for (;; i = (i + 1) & g_disp_mask) {
        const RtDispEntry *e = &g_disp[i];
        if (__builtin_expect(e->guest == target, 1)) return e->fn;
        if (e->guest == 0) { RT_THREAD(ctx)->miss_target = target; return rt_dispatch_miss; }
    }
}
```

- **Keys.** Every entry of every lifted module's `<module>_table.c` as `base + rva`
  (about 160,000), every shim stub, every trap stub, every native-object method stub. The
  table has a row for each function the lifter found, lifted or not: a skipped function's
  row is a stub that stops with the lifter's reason (`rt_trap_skipped`),
  so calling one is not a miss. All
  bases are fixed at start-up (section 1.2) and trap stubs are created by reading every
  manifest module's import table at start-up, so nothing is inserted later.
- **Size.** 2^19 entries, 8 MB, load factor about 0.3.
- **Shim addresses.** Shim *i* of a native module (sorted by name) lives at
  `module base + 0x10000 + 16*i`. The 16 bytes are filler that reads as
  `mov eax, i; ret; int3...` and is never executed.
- **A miss** calls `rt_dispatch_miss`, the single hook. It reports the target as
  `module+rva` and whether the address is inside any image at all (a target in the VM
  arena means the game generated code), then aborts with the guest caller. Inside a lifted
  module a miss means an address that is not the start of a function the lifter found: a
  function it found and skipped has a row and stops in its stub with the reason. The
  interpreter fallback in the earlier plan would replace this hook; it is out of scope until
  Gate 3.
- **Cost per indirect call:** one multiply, one shift, one 16-byte load, one compare, then
  the indirect branch. Expected a few nanoseconds on a cache hit; to be measured, not
  assumed **(E5)**. An import call costs the same.

Changes this needs in the linked-build macros of `harness/runtime.h` (owned by the lifter):

```c
#define CALL_INDIRECT(target, ret_rva) do { uint64_t t_ = (target); RT_SAVE_RSP();     \
        TF_PUSH64(BASE + (ret_rva)); rt_lookup(ctx, t_)(ctx); RT_CHECK_RSP(); } while (0)
#define TAIL_INDIRECT(target) do { __attribute__((musttail)) return rt_lookup(ctx, (target))(ctx); } while (0)
#define TAIL_DIRECT(fn, R)    do { __attribute__((musttail)) return fn(ctx); } while (0)
```

**Why.** Virtual calls are the engine's main control structure (86,028 `call [reg+disp]`
sites), so the lookup must be a handful of instructions with one cache line touched. A hash
over a few hundred thousand keys does that in 8 MB. Building it once and write-protecting
it removes every concurrency question.

`musttail` answers this: a guest tail call must not grow the host stack, and it
can always be honoured because lifted functions, thunks and trap stubs all have the type
`void (Ctx *)`. The second half of item 13 is that `RET` trusts the host stack.
`RT_CHECK_RSP` is the answer for now: in checked builds (the default through Gate 3) every
call site verifies that the guest `rsp` came back to its value before the push, and traps
with the call site if not. That catches `longjmp`, unwinding and stack switching at the
first frame they skip instead of later as corruption. Making them work is section 9.3.

*Rejected:* binary search of the per-module tables on every call (about 15 probes); a table
parallel to the code with one entry per code byte (about 270 MB touched for 34 MB of code);
rewriting vtable slots to hold host pointers (changes values the guest can see, and breaks
RTTI and `RtlPcToFileHeader`); per-call-site inline caches and direct calls for import
slots (worthwhile later, after profiles; shim symbols are named predictably so the lifter
can emit them).

## 3. Shim ABI

### 3.1 What a shim sees

A shim's dispatch-level type is `void shim_<lib>_<Name>(Ctx *ctx)`, entered exactly as a
lifted function would be: the caller has pushed the return address.

| Windows x64 item | Where it is on entry |
|---|---|
| Integer or pointer argument 0 to 3 | `rcx`, `rdx`, `r8`, `r9` = `ctx->r[1]`, `r[2]`, `r[8]`, `r[9]` |
| Float or double argument 0 to 3 | `ctx->xmm[0..3]`, low lane, chosen by position |
| Argument *n* >= 4 | 8 bytes at `rsp + 0x28 + 8*(n-4)` |
| Home space | `rsp + 8` to `rsp + 0x28`; the shim may overwrite it |
| Return address | 8 bytes at `rsp` |
| Struct argument of 1, 2, 4 or 8 bytes | By value in the argument's register or slot |
| Any other struct argument | Pointer to the caller's copy |
| Return value | `rax`; float or double in `xmm0` |
| Struct return that is not 1, 2, 4 or 8 bytes | Caller passes the destination as a hidden argument 0 and the rest shift up; `rax` returns that pointer. For a C++ method, `this` stays argument 0 and the hidden pointer is argument 1 |

The upper 32 bits of a 32-bit argument are **undefined** and must be ignored. A shim leaves
`rbx rbp rsi rdi r12-r15`, `xmm6-xmm15` and the direction flag as it found them, and returns
with `rsp` 8 higher than on entry. Everything else may be clobbered.

### 3.2 How a shim is written

**Decision.** The author writes one typed C function per import, introduced by a one-line
`SHIM` declaration. A generator reads those lines and writes the thunk that does the
marshalling in 3.1 and the table that registers the shim. Authors do not marshal by hand.

```c
/* runtime/shims/kernel32/ReadFile.c */
#include "rt/shim.h"

SHIM(kernel32, BOOL, ReadFile, HANDLE hFile, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov)
{
    RtFile *f = (RtFile *)rt_handle_ref(hFile, RT_K_FILE);
    if (!f) return FALSE;                               /* rt_handle_ref set ERROR_INVALID_HANDLE */
    if (ov) RT_UNIMPLEMENTED(ctx, "ReadFile with OVERLAPPED");
    ...
    return FAIL(ctx, rt_win32_from_errno(errno, RT_ERR_FILE), FALSE);
}
```

```c
/* rt/shim.h (sketch) */
#include "runtime.h"        /* Ctx, LD*, ST*: the lifter contract, never redeclared here */
#include "rt/wintypes.h"

#define SHIM(lib, ret, name, ...)        ret impl_##lib##_##name(Ctx *ctx __VA_OPT__(,) __VA_ARGS__)
#define SHIM_ORD(lib, ord, ret, name, ...)  SHIM(lib, ret, name, __VA_ARGS__)   /* also exported as #ord */
#define SHIM_RAW(lib, name)              void shim_##lib##_##name(Ctx *ctx)     /* you marshal: varargs only */

/* Raw access, used by generated thunks and by SHIM_RAW bodies. n counts from 0. */
static inline uint64_t ARG_U64(Ctx *ctx, unsigned n);    /* register, or LD64(rsp + 0x28 + 8*(n-4)) */
static inline float    ARG_F32(Ctx *ctx, unsigned n);    /* xmm[n] low lane, or the stack slot */
static inline double   ARG_F64(Ctx *ctx, unsigned n);
#define RET_U64(ctx, v)  ((ctx)->r[0] = (uint64_t)(v))
#define RET_U32(ctx, v)  ((ctx)->r[0] = (uint64_t)(uint32_t)(v))      /* upper half written as zero */
static inline void RET_F32(Ctx *ctx, float v);
static inline void RET_F64(Ctx *ctx, double v);

/* Last error lives in the guest thread block at the Windows offset (section 6.2). */
static inline void  rt_set_last_error(Ctx *ctx, DWORD e) { ST32(ctx->gs_base + 0x68, e); }
static inline DWORD rt_get_last_error(Ctx *ctx)          { return LD32(ctx->gs_base + 0x68); }
#define FAIL(ctx, err, retval)   (rt_set_last_error((ctx), (err)), (retval))

_Noreturn void rt_unimplemented(Ctx *ctx, const char *file, int line, const char *what);
#define RT_UNIMPLEMENTED(ctx, what)  rt_unimplemented((ctx), __FILE__, __LINE__, (what))
```

What the generator emits for the example (never edited by hand):

```c
void shim_kernel32_ReadFile(Ctx *ctx)
{
    RT_SHIM_ENTER(ctx, RT_ID_kernel32_ReadFile);      /* host float mode, in-host mark, call trace */
    BOOL r = impl_kernel32_ReadFile(ctx, (HANDLE)ARG_U64(ctx, 0), (LPVOID)ARG_U64(ctx, 1),
                 (DWORD)ARG_U64(ctx, 2), (LPDWORD)ARG_U64(ctx, 3), (LPOVERLAPPED)ARG_U64(ctx, 4));
    RET_U32(ctx, r);
    RT_SHIM_LEAVE(ctx);                               /* trace, guest float mode, rsp += 8 */
}
```

Rules of the `SHIM` line: it is one line; every parameter type and the return type must be
in the generator's type table (`runtime/tools/types.def`, core-owned), which maps each
Windows type name to a class (32-bit signed or unsigned, 64-bit, pointer, handle, float,
double, 8-byte struct by value, guest code address, struct return). An unknown type is a
generator error, not a guess.

**Why.** Three things contributors get wrong when they marshal by hand are fixed mechanically:
the argument's position, integer versus float register, and truncating 32-bit arguments.
The declaration is also the single description of each function's arguments, which the
call tracer needs in order to print them (section 9.5), and which lets a test call the
function either as plain C or through the guest ABI. Because the generator collects the
lines, a shim registers itself by existing: no shared registry file for contributors to collide
in.

*Rejected:* hand-written `void f(Ctx *)` bodies with `ARG(n)` macros (error-prone, no
argument description for tracing); `_Generic` macro marshalling with no generator
(unreadable, cannot build the table); registration by constructor or linker section
(works, but leaves no argument description).

### 3.3 Types

**Decision.** `rt/wintypes.h` defines the familiar Windows names with fixed widths: `BOOL`,
`INT`, `LONG` are `int32_t`; `DWORD`, `UINT`, `ULONG` are `uint32_t`; `WORD`, `WCHAR` are
`uint16_t`; `SIZE_T`, `ULONG_PTR`, `WPARAM` are `uint64_t`; `LONG_PTR`, `LPARAM`, `LRESULT`
are `int64_t`; `HANDLE` and the other handle types are `void *`; `GUESTFN` (and names such
as `WNDPROC`, `LPTHREAD_START_ROUTINE`) is `uint64_t`, a guest code address that C cannot
call directly. Because guest address equals host address, a guest pointer argument is used
as an ordinary C pointer.

Guest-visible structs live in core-owned headers under `rt/win/`, declared with these
types, no bit-fields, and a `_Static_assert` on the size and on every offset the code
uses. **`long`, `unsigned long`, `wchar_t` and `size_t` are forbidden in any guest-visible
type or shim signature** (they differ between the two platforms: `long` is 64-bit and
`wchar_t` is 32-bit on macOS). Shim files do not include Objective-C or Cocoa headers
(their `BOOL` is a different type).

**Why.** arm64 macOS and x64 Windows agree on layout for fixed-width integers, pointers and
doubles, so plain C structs can describe guest memory, but only if the four types that
differ never appear. A lint enforces it (section 10.5).

*Rejected:* prefixed names such as `W_DWORD` (contributors copy prototypes from the Microsoft
documentation, so the documented names must simply be right); including real Windows
headers (their `long` and `wchar_t` are the wrong size here).

### 3.4 Last error

**Decision.** A shim sets last error on failure and leaves it alone on success, unless the
Microsoft documentation for that function says otherwise (`TlsGetValue` clears it on
success). `CreateEvent`, `CreateMutex` and `CreateSemaphore` set it on every success:
`ERROR_ALREADY_EXISTS` when the name existed, as documented, and 0 when a new object was
made, which the documentation does not state (not verified on Windows; it is what makes the
usual test `GetLastError() == ERROR_ALREADY_EXISTS` after a successful call safe). Errors
from host calls go through one function, `rt_win32_from_errno(errno, context)`; a shim never
invents the mapping. The value is stored in the guest thread block at offset `0x68`, where
inlined guest code would look for it. A host thread that is not a guest thread (start-up
before thread 1 is attached, a host thread of a test) has no thread block; for it
`rt_thread_set_last_error` keeps the value in a variable local to that host thread.

Who stores the value differs between core units, and each header states its rule at the
top: most core functions set last error themselves on failure, so the shim only returns
the result; `env` hands the code back through an out parameter, `path` returns it, and
`registry` returns the status that the `Reg*` function itself returns, and those three
never touch last error. The table is in `runtime/README.md`.

**Why.** Guest code branches on `GetLastError` after a failure, and the C runtime turns it
into `errno`; a wrong code changes behaviour far from the shim. One mapping function keeps
the 739 shims consistent.

*Rejected:* a host thread-local for last error (invisible to guest code that reads the
thread block directly); setting it on every call (overwrites values the caller still
wants).

### 3.5 Calling guest code from a shim

**Decision.**

```c
uint64_t rt_guest_call(Ctx *ctx, GUESTFN fn, unsigned nargs, const uint64_t *args);
#define GUEST_CALL(ctx, fn, ...)  /* integer and pointer arguments; returns rax */
double   rt_guest_call_fp(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs);   /* mixed classes */
```

`rt_guest_call` builds a fresh frame below the current guest `rsp` (16-byte aligned, 32
bytes of home space, arguments 4 and up above it), loads the argument registers, pushes the
address of a `rt_return_to_host` stub as the return address, switches to the guest float
mode, calls `rt_lookup(ctx, fn)(ctx)`, checks that `rsp` came back, and restores the shim's
`rsp`. This one primitive serves `DllMain`, TLS callbacks, thread start routines, window
procedures, enumeration callbacks, timer and APC routines, console control handlers, FLS
destructors and comparators.

Rules:

1. `ctx` must belong to the calling thread, and that thread must be a guest thread. A host
   thread the runtime did not create (an SDL audio callback, a dispatch queue) calls
   `rt_thread_attach()` first, which gives it a thread block, a guest stack, TLS blocks and
   the two CPU modes of section 6.1.
2. Hold no runtime lock across a guest call except the loader lock. The guest may re-enter
   any shim.
3. After the call every volatile guest register is garbage; the shim's own C locals are
   intact.
4. If the guest callee unwinds past the shim (an exception or `longjmp` through a
   callback), the `rsp` check traps. That is plan A (section 9.3).

**Why.** A callback is a guest function, so it must be entered the way lifted code enters
any function: through the dispatch table, with a real return address on the guest stack so
that guest stack walkers see a sane frame.

*Rejected:* a separate guest stack or `Ctx` per callback (the callee expects to run on the
caller's thread and stack); letting shims look functions up and call them directly (skips
the frame, the float mode and the `rsp` check).

### 3.6 Float mode at the boundary **(E4)**

**Decision.** Host code always runs with the host's default FPCR. If a later measurement decides that
lifted code runs with the x86-compatible bits set, `RT_SHIM_ENTER` restores the default and
`RT_SHIM_LEAVE` puts the guest value back; `rt_guest_call` does the reverse. Shim authors
never touch FPCR. The x86 memory-ordering mode is different: it stays on for the life of a
guest thread, inside shims too.

**Why.** An earlier experiment showed the bits change every float instruction on the thread,
including code in system libraries that nobody has shown to be indifferent. The thunk is
the one place every crossing passes through, so the policy can change there without
touching a shim. Whether hot shims that do no float work may skip the switch waits for the
cost measurement.

*Rejected:* running host code in the guest float mode (unproven for system libraries);
leaving the choice to each shim author.

### 3.7 COM and other vtable interfaces

Direct3D, DXGI, the audio COM classes and the Source engine's own `CreateInterface`
objects are all the same thing to lifted code: a pointer to an object whose first word
points to a table of function addresses, called with the Windows convention and `this` in
`rcx`.

**Decision.** Interfaces are described once each, in core-owned files under
`runtime/com/` (name, IID, base, methods in vtable order with `SHIM`-style prototypes). A
generator produces, per interface, a guest-visible vtable in the owning native module's
slot and one dispatch-registered stub per method. There are two ways to stand behind it:

- **Guest-ABI native object.** For objects we write ourselves (the Gate 3 null Direct3D
  device, audio device enumerators, a native replacement for a game interface). The object
  is allocated with `rt_galloc`, begins with `{ vtable address, reference count, class }`,
  and each method is a typed C function exactly like a shim, taking `this` first. No
  translation layer.
- **Proxy for a host C++ object.** For DXMT at Gate 4. The guest holds a small proxy
  `{ guest vtable, native pointer, class }`; each generated method thunk unwraps `this`,
  converts arguments, calls the native method, and wraps any interface it returns. One
  proxy per (native pointer, interface), found through a map, so pointer identity holds.
  Structs are passed through unchanged unless the generator is told their layouts differ
  (wide-character arrays, `long` fields).
- **The other direction** (the guest hands us an object it implements) uses a generated
  host-side object whose methods call `rt_guest_call` on the guest vtable. Nothing at
  Gate 3 needs it.

`CoCreateInstance` for a class we do not provide returns `REGDB_E_CLASSNOTREG` and logs the
CLSID. `D3D11CreateDevice` is an ordinary shim that returns one of the above.

**As built** (`runtime/include/rt/com.h` is the specification). The descriptions are
`runtime/com/desc/<Interface>.idef`, generated from the MinGW-w64 headers by
`runtime/com/gen_desc.py` and checked in; `runtime/tools/gen_com.py` writes the thunks at
build time. Two things differ from the sketch above. An object does not begin with
`{ vtable address, reference count, class }`: it has one *face* per interface chain it
presents (a device is both `ID3D11Device` and `IDXGIDevice1`, which share no vtable), each
face begins with its vtable address, and the count and the class are in a header the faces
point to. And a method is not written "exactly like a shim" in a file that the build must
be told about: its body is `<bodies>/<Interface>/<Method>.c` with a one-line `COM_METHOD`
declaration that the generator checks against the description, and a method with no body is
a stop that names itself. Calls are counted per (interface, method).

**Why.** A host C++ vtable cannot be shown to the guest directly: a slot holds a host
address that says nothing about its signature, and the calling conventions differ. A
generated bridge from one interface description keeps interfaces with a hundred or more
methods mechanical and
gives the tracer the same argument descriptions as for flat shims. Writing our own objects
at the guest ABI avoids a pointless second layer for code that has no native ABI to
respect.

*Rejected:* exposing DXMT objects directly; hand-written thunks per method.

## 4. Handles

### 4.1 One table for kernel objects

**Decision.** A handle is a 32-bit value, `(generation << 22) | (index << 2)`: a multiple
of 4, never 0, never negative. Lookups use only the low 32 bits and ignore the low two, as
Windows does. The table maps a handle to a reference-counted object with a kind:

```c
typedef enum { RT_K_FILE = 1, RT_K_EVENT, RT_K_MUTEX, RT_K_SEMAPHORE, RT_K_THREAD, RT_K_PROCESS,
               RT_K_SECTION, RT_K_FIND, RT_K_PIPE, RT_K_SOCKET, RT_K_REGKEY } RtKind;

RtObject *rt_object_new(RtKind kind, size_t size, const RtObjectOps *ops,
                        const WCHAR *name, bool *existed);     /* named objects share one namespace */
HANDLE    rt_handle_new(RtObject *obj);                         /* consumes the caller's reference */
RtObject *rt_handle_ref(HANDLE h, RtKind kind);                 /* kind 0 = any; +1 reference; on failure
                                                                   NULL and ERROR_INVALID_HANDLE */
void      rt_object_unref(RtObject *obj);
BOOL      rt_handle_close(HANDLE h);
HANDLE    rt_handle_dup(HANDLE h);                              /* new slot, same object */
```

- Files, events, mutexes, semaphores, threads, the process, file-mapping objects,
  find-file state, pipes, sockets and open registry keys are in the table.
- **Modules are not.** An `HMODULE` is the image base and is looked up in the module
  registry.
- **Heaps are not.** A heap handle is the guest address of a small heap header, checked by
  a magic number, as on Windows where it is a pointer.
- Pseudo-handles `(HANDLE)-1` (current process) and `(HANDLE)-2` (current thread) are
  resolved by `rt_handle_ref` wherever Windows accepts them. `DuplicateHandle` on one makes
  a real handle. Closing one has no effect.
- **Handle inheritance does not exist.** `bInheritHandle` TRUE, in `DuplicateHandle`, in
  the `Open*` functions and in a `SECURITY_ATTRIBUTES`, is accepted and ignored: the runtime
  never creates a child process, so there is nothing to inherit into.
- **A named object is visible before its creator has filled it in**: `rt_object_new` puts
  the name into the name space at once. The component that owns a named kind creates and
  initialises under one lock that its `Open` function also takes (the dispatcher lock for
  waitable kinds); the rule is spelled out in `rt/handle.h`.
- `CloseHandle` drops the slot and one reference; the object dies with its last reference,
  so a thread blocked in a wait keeps its object alive. Find-file handles close only
  through `FindClose`; `CloseHandle` on one fails and is logged.
- Window, GDI and other user-interface handles use the same code with a second table
  instance at Gate 4, so mixing the two spaces fails cleanly.

**Why.** One table with kinds gives `CloseHandle`, `DuplicateHandle` and the wait functions
a single implementation. The generation bits make a stale handle fail loudly instead of
silently naming a newer object, which is the bug most likely to appear in our own shims.
Heaps stay out because `HeapAlloc` is on the allocation path and its handle is never
closed or waited on.

*Rejected:* one table per kind (three places to implement `CloseHandle`); host pointers or
file descriptors as handle values (not 32-bit clean, no kind check).

### 4.2 Waiting

**Decision.** One dispatcher lock and one wait core:

```c
DWORD rt_wait(Ctx *ctx, unsigned n, RtObject *const *objs, bool wait_all, DWORD timeout_ms, bool alertable);
void  rt_signal(RtObject *obj);      /* called with the dispatcher lock held, after changing obj's state */
```

Each waitable kind supplies `is_signaled(obj, thread)` and `consume(obj, thread)`. `rt_wait`
checks under the lock whether the wait can be satisfied; if not, it queues the thread on
every object, first in first out, and the thread sleeps. A signaller, under the same lock,
wakes threads whose whole wait is now satisfiable and consumes on their behalf (an
auto-reset event resets, a semaphore count drops, a mutex gets its owner). Signalled
states: events as set; mutexes when unowned or owned by the caller (recursive, with
abandonment at thread exit); semaphores when the count is positive; threads and the
process when exited.

As built (`core/wait.c`; `rt/handle.h` and `rt/sync.h` are the specification):

- **The thread sleeps on a 32-bit word of its own wait record** with
  `os_sync_wait_on_address`, the primitive of section 6.4, not on a condition variable.
- **The dispatcher lock is an `os_unfair_lock`**, not a pthread mutex. Measured with eight
  threads and 160,000 lock and unlock pairs: 561 to 830 ms with a fair pthread mutex, 1.8
  to 5.8 ms with the unfair lock. It is not recursive and knows its holder: taking it twice
  or releasing it unheld is a fatal report. Two system calls are made under it: waking a
  sleeper, and allocating the object in the `Create*` functions (which is what keeps a
  named object from being found half built, section 4.1).
- **Argument errors are returned, not fatal.** `rt_wait` with no object or more than 64, or
  a wait-all that names one object twice, fails with `ERROR_INVALID_PARAMETER`; an object
  whose kind is not waitable fails with `ERROR_INVALID_HANDLE` (Windows may say
  `ERROR_ACCESS_DENIED` for registry keys and sections; not verified).
- **`consume` returns nothing.** A mutex that was abandoned by its previous owner reports
  it by calling `rt_wait_report_abandoned()` from its `consume`, and the wait then returns
  `WAIT_ABANDONED_0 + index`.
- **An alertable wait** that finds an APC queued runs every queued APC, those queued while
  it runs them included, with no lock held, and returns `WAIT_IO_COMPLETION` having taken
  nothing from any object. On entry the order is: the objects, then a queued APC (even with
  timeout 0), then the timeout (the kernel's order as far as known; not verified).
- **Timeouts** are deadlines on the runtime's monotonic clock (section 8.1), fixed when the
  thread is queued. The host sleep is measured on a clock that stops while the machine
  sleeps, the only one the primitive offers, so after the machine wakes a wait can last
  longer than its timeout. With a test clock (`rt_time_set_source`) a sleeping wait reads
  the clock again only at its own wake-ups.
- **A sleep is not a wait on nothing.** `Sleep` and `SleepEx` are `rt_sleep_ex`.

**Why.** `WaitForMultipleObjects` with wait-all has to observe and consume several objects
atomically; a single lock makes that trivially right, and the critical sections are a few
instructions. The hot locks of the engine (critical sections, SRW locks, `SList`) are not
handles and never take this lock (section 6.4).

*Rejected:* mapping each object to its own pthread primitive (no correct wait-any or
wait-all); a helper thread per wait; a pthread condition variable per thread, which is what
this section first named (it needs a pthread mutex as the dispatcher lock, and the fair one
was two orders of magnitude slower under contention).

## 5. Memory

### 5.1 What the guest is told

`GetSystemInfo` reports a 4 KB page, 64 KB allocation granularity, and an address range of
`0x10000` to `0x7FFFFFEFFFF` (8 TB). The runtime never hard-codes the host page size; it
asks at start-up.

### 5.2 `VirtualAlloc` family on 16 KB pages **(E3)**

**Decision.** A VM map (core-owned) covers both arenas. A *region* is one
reservation: base 64 KB aligned, taken first-fit from the VM arena, or at the requested
address if that range is free inside the arena. Each 4 KB guest page in a region has a
state (free, reserved, committed) and a protection, held in a side array.

```c
uint64_t rt_vm_alloc(uint64_t addr, uint64_t size, DWORD alloc_type, DWORD protect, RtVmType type);
BOOL     rt_vm_free(uint64_t addr, uint64_t size, DWORD free_type);
BOOL     rt_vm_protect(uint64_t addr, uint64_t size, DWORD new_protect, DWORD *old_protect);
SIZE_T   rt_vm_query(uint64_t addr, MEMORY_BASIC_INFORMATION *out);
```

- A host page is accessible when any guest page inside it is committed, with the **union**
  of those pages' protections. Reads and writes to a reserved neighbour inside the same
  host page therefore do not fault.
- Commit yields zeros. Decommit of a whole host page drops it; decommit of part of a host
  page zeroes those 4 KB pages at once, so a later commit sees zeros either way.
- `VirtualProtect` (reachable only through `GetProcAddress` today) updates the side array,
  recomputes the union, and returns the exact old value. Execute permissions are recorded
  and treated as read: guest bytes are never executed. A call into such memory is a
  dispatch miss that says so.
- `VirtualQuery` answers from the side array at 4 KB precision, with `MEM_IMAGE` and the
  module base as `AllocationBase` for images, `MEM_MAPPED` for file views, `MEM_PRIVATE`
  otherwise. For an address outside both arenas (host heap, host stack) it asks the kernel
  and reports committed private memory.
- `PAGE_GUARD` and `PAGE_NOACCESS` are exact only when they cover whole host pages;
  otherwise they are recorded and logged once. `MEM_LARGE_PAGES` fails with
  `ERROR_PRIVILEGE_NOT_HELD`. `MEM_RESET` succeeds and does nothing.

**Why.** Guest code and the Microsoft C runtime hard-code 4096 in places (the stack probe
steps by it), so reporting 16 KB is not an option; and an allocator built on `VirtualAlloc`,
which tier0's 11 call sites suggest it has, normally reserves a large range and commits it
piecemeal, so partial commits must work. The union rule can only make memory more
accessible than Windows would, so correct programs cannot tell; the cost is that a wild
access into an uncommitted or guard 4 KB page may not fault. With exceptions on plan A
nothing could catch that fault anyway. macOS 26 has `posix_spawnattr_set_4k_page_size_np`;
if a native process can run with 4 KB pages, the union rule becomes exact with no code
change.

*Rejected:* reporting a 16 KB page size; rounding every commit to 16 KB without tracking
(wrong `VirtualQuery` answers, stale data after decommit).

### 5.3 Guest stacks and the stack limit

**Decision.** A guest stack is one region in the VM arena, fully committed when the thread
is created, with a 64 KB inaccessible host guard below it. The thread block's `StackBase`
(`+0x08`) is the top, `StackLimit` (`+0x10`) is the lowest usable address, and
`DeallocationStack` (`+0x1478`) is the region base. Size is the larger of the requested
size and 1 MiB, rounded up to 64 KB; 8 MiB for the first thread. Nothing is committed on
demand.

**Why.** The 24 `gs:[0x10]` reads are the stack probe (one per statically linked C
runtime). The probe compares the new stack pointer with `StackLimit` and only touches
pages below the limit; with the whole stack committed and the limit at its bottom, the
probe does nothing in normal operation, and a real overflow walks into the guard and
faults, which section 9 reports as a guest stack overflow. No guard-page emulation is
needed. Untouched stack pages cost no physical memory.

*Rejected:* committing the stack on demand through an emulated guard page (needs fault
handling that plan A does not have, for no benefit on a 64-bit host).

### 5.4 Heaps

**Decision.** `GetProcessHeap` is a dedicated host malloc zone (not the default zone);
each `HeapCreate` heap is another zone, destroyed whole by `HeapDestroy`. Every block has a
16-byte header `{ magic, heap id, requested size }` and the caller gets the address after
it, 16-byte aligned. `HeapSize` returns the requested size exactly. `HeapFree` and
`HeapReAlloc` check the magic and the heap id and trap on a foreign pointer.
`HEAP_ZERO_MEMORY` zeroes exactly the requested bytes, including growth on reallocation.
`HeapSetInformation` succeeds and does nothing. `rt_galloc` and `rt_gfree` give runtime
code guest-visible memory from the process heap.

As built (`core/heap.c`; `rt/heap.h` is the specification):

- **Fixed-size heaps.** `HeapCreate` with a nonzero maximum makes a heap that cannot grow.
  One block must be smaller than `0x7FFF8` bytes, and the total is held to the maximum
  rounded up to a 4 KB page, each block counted as 16 bytes plus its size rounded up to
  16. A request beyond either limit returns NULL like any exhausted heap. The block limit
  is the figure of the `HeapAlloc` page; the `HeapCreate` page says "slightly less than
  1,024 KB" for a 64-bit process. The core's figure stays until a caller shows it matters
  (which one Windows 7 enforces is not verified).
- **Caps.** At most 1,024 heaps alive at once (one more is fatal). A request of 4 TiB or
  more returns NULL without the host being asked. The commit limit of Windows is not
  emulated. A host zone that returns a block above 8 TB is fatal (section 1.2).
- **`HeapReAlloc` to size 0 succeeds** and returns a live block of size 0, exactly what
  `HeapAlloc(0)` gives; the block is not freed (the documentation is
  silent and Windows was not consulted).
- **`HeapDestroy` of the process heap fails** with `ERROR_INVALID_HANDLE` and destroys
  nothing (the code is from memory of kernel32, not verified).
- **`HeapValidate`** (`rt_heap_validate`) applies the check of `HeapFree` and answers
  instead of stopping; with no block it answers for the heap handle only, because there is
  no list of blocks to walk.
- **Flags.** A flag bit a function does not take is fatal, not ignored.
  `HEAP_NO_SERIALIZE` changes nothing (every zone is thread-safe).
  `HEAP_GENERATE_EXCEPTIONS` turns an allocation failure into a fatal report (section 9.3).
- **Last error.** `HeapAlloc` and `HeapReAlloc` never set it, as their pages say.

**Why the simple design is enough.** The game brings its own allocator: modules allocate
through tier0's allocator object (995 reference sites in vstdlib against 1 for `HeapAlloc`),
and tier0 itself has 11 `VirtualAlloc` sites and 4 `HeapAlloc` sites. The Heap family is
what the C runtime uses for its own bookkeeping, and possibly what tier0's allocator falls
back to; how much traffic it carries is not known **(E9)**. Host zones are fast enough for
either case and keep host memory tools usable on guest allocations. The header exists
because the C runtime's `_recalloc` zero-fills from the old `HeapSize`; a size rounded up
by the host allocator would leave garbage where Windows has zeros. A zone per heap gives
`HeapDestroy` its meaning and keeps guest blocks apart from the runtime's own, which is
also why `rt_galloc` exists: memory the guest may later hand to `HeapFree`, `LocalFree` or
`CoTaskMemFree` must have come from the heap those functions expect.

*Rejected:* an allocator of our own inside the VM arena (large, and unneeded unless E2
shows zone addresses above 8 TB in the real process, in which case an embedded allocator
over a reserved range is the fallback); passing `malloc_size` through as `HeapSize`.

## 6. Threads

### 6.1 Creating a guest thread

**Decision.** One host thread per guest thread. `rt_thread_create`, behind `CreateThread`:

1. Allocate an `RtThread` (host memory). It embeds the thread's `Ctx`; `ctx.host` points
   back to it (`RT_THREAD(ctx)`), and a host thread-local gives the current one to code
   that has no `ctx`.
2. In the VM arena: a 16 KB thread block (6.2), the guest stack (5.3), the TLS pointer
   array with one slot per manifest module, and a copy of the TLS template of every loaded
   module that has one.
3. Create the thread object in the handle table and assign a thread id (a multiple of 4,
   never reused).
4. Start a host thread with an explicit stack of four times the guest stack, at least
   4 MiB **(E5)**, and an alternate signal stack.
5. On the new thread, before any guest code: `thread_set_x86_64_compat(1)`; set the guest
   float mode; `mxcsr = 0x1F80`, direction flag clear, `gs_base` = thread block. If the
   thread was created suspended, wait here for the first `ResumeThread`.
6. Run the thread-attach calls (1.6), then the start routine through `rt_guest_call`.

The first guest thread is the host's main thread, attached in `rt_init`, with its host
stack enlarged at link time. The x86 ordering call must succeed when the runtime runs from
the app bundle (`RT_TSO=require`, fatal otherwise). Unit tests run as plain command-line
programs where the call is refused; they use `RT_TSO=try`.

**Why.** Guest threads block in system calls, hold host locks inside shims and are
scheduled by the host; a one-to-one mapping is the only model that keeps the hardware
ordering mode (per thread, not inherited) and the debugger's view simple. The host stack
ratio is a guess that E5 replaces: lifted code uses one host frame per guest frame, so host
depth follows guest depth. The first guest thread must be the host main thread because the
window system only works there, and the engine's first thread is assumed to be the one that
owns the window (11.3).

*Rejected:* green threads or a single host thread running all guest threads (kept only as
the earlier plan's fallback, reached by reporting two processors, see 8.5).

### 6.2 The thread block

`ctx->gs_base` points at a block laid out as the Windows x64 TEB. The runtime keeps its own
per-thread state in these fields, at these offsets, so that inlined guest reads agree with
the shims. Everything else is zero.

| Offset | Field | Value |
|---|---|---|
| `0x08`, `0x10` | StackBase, StackLimit | Section 5.3 |
| `0x30` | Self | Address of the block |
| `0x40`, `0x48` | Process id, thread id | What `GetCurrentProcessId` and `GetCurrentThreadId` return |
| `0x58` | ThreadLocalStoragePointer | The TLS pointer array (static TLS) |
| `0x60` | PEB pointer | A process block with `BeingDebugged` 0, image base, process heap, processor count and the OS version of section 8.5 |
| `0x68` | LastErrorValue | What `GetLastError` returns |
| `0x1478` | DeallocationStack | Guest stack region base |
| `0x1480` | TlsSlots[64] | `TlsGetValue` for indices 0 to 63 |
| `0x1780` | TlsExpansionSlots | Pointer to 1,024 more slots, allocated on first use by that thread |

**Why.** The lifter turns `gs:[x]` into a load from `gs_base + x` with no hook, and 21 of
the 277 `gs:` sites read the self pointer or a computed offset, so the block must be right
wherever the guest may look, not only at the three offsets counted so far (E7).

*Rejected:* keeping thread id, last error and TLS slots in host structures and exposing
only offsets `0x10`, `0x30` and `0x58`.

### 6.3 Exit, TLS slots, fibers, priorities, sleeping

**Decision.**

- **Exit.** Returning from the start routine and `ExitThread` take the same path:
  `ExitThread` unwinds the host stack with a `longjmp` to the thread's base frame. Then:
  FLS callbacks for non-null slots, `DLL_THREAD_DETACH` in reverse load order, release held
  mutexes as abandoned (`rt_wait_thread_exit`, called once, on the exiting thread, after
  its last guest code and before the thread object is signalled; it also drops the APCs
  still queued to the thread, which never run), mark the thread object signalled with its
  exit code, clear `RtThread.sub[RT_SUB_HANDLE]` and drop the thread subsystem's reference to
  the thread object (in that order: `rt/thread.h`), free the TLS blocks, guest stack and
  thread block. The `RtThread` lives until its last handle closes.
  If the first thread returns, the process exits.
- **`TlsAlloc`.** A process-wide bitmap of 1,088 indices. `TlsFree` zeroes the slot in every
  thread. `TlsGetValue` clears last error on success.
- **`Fls*`.** A separate index space with an optional callback per index, kept host-side.
  **Fibers are not supported**: no module imports or names a fiber function,
  so fiber-local storage is thread-local storage with exit callbacks.
- **Priorities and affinity are recorded and otherwise ignored.** `SetThreadPriority` and
  `GetThreadPriority` round-trip; `SetThreadAffinityMask` returns the previous mask;
  `GetProcessAffinityMask` reports one bit per reported processor; `SetPriorityClass`,
  `SetThreadPriorityBoost`, `SetThreadIdealProcessor` and the two `avrt` calls succeed.
  Guest threads run at the user-interactive quality-of-service class.
- **Sleeping.** `Sleep(0)` and `SwitchToThread` yield. `Sleep(n)` sleeps *n* milliseconds
  with no 15 ms rounding (tier0 calls `timeBeginPeriod` anyway, which succeeds and does
  nothing). `Sleep` and `SleepEx` are `rt_sleep_ex`, which is the wait core with no object;
  the `Ex` waits are `rt_wait_handle[s]` with the alertable flag. An alertable sleep or
  wait runs every APC queued to the thread (`QueueUserAPC`, `rt_apc_queue`) and returns
  `WAIT_IO_COMPLETION`; a thread that never waits alertably never runs them. Queueing to a
  thread that has exited fails with `ERROR_GEN_FAILURE` (Wine `kernel32/tests/sync.c`
  `test_QueueUserAPC`).
  Waitable timers are not imported and not designed.
- **Suspension.** A thread created suspended waits at step 5 of 6.1 and that is fully
  supported. `SuspendThread` on a running thread, `GetThreadContext`, `SetThreadContext`
  and `TerminateThread` trap until a real caller is seen; tier0's only users are a
  hardware-breakpoint helper no module calls and `CThread` methods.

**Why.** macOS on Apple silicon has no thread affinity, and nothing in the engine's
correctness depends on priority; storing the values keeps the engine's own bookkeeping
consistent. Stopping an arbitrary running thread is the one thread feature that is
genuinely dangerous here, because the victim may hold a host lock inside a shim; it is not
worth designing before something needs it. The design that would be used is known:
suspend with the Mach call, and retry if the victim is inside a shim (the thunks maintain
that mark).

### 6.4 Locks the game embeds in its own structures

The game allocates these inside its objects and statics, so their sizes and observable
fields must match Windows. All of them block on one primitive,
`os_sync_wait_on_address` (macOS 14.4 and later), applied to a word inside the guest
structure. None allocates, none uses the handle table.

| Structure | Size | Layout we keep | Notes |
|---|---|---|---|
| `CRITICAL_SECTION` | 40 | `+0x00` DebugInfo = -1; `+0x08` LockCount (-1 free, 0 held); `+0x0C` RecursionCount; `+0x10` OwningThread (thread id); `+0x18` LockSemaphore (low 32 bits are our wait word: 0 free, 1 held, 2 contended); `+0x20` SpinCount | Recursive. `OwningThread` and `RecursionCount` are maintained truthfully because debug code reads them. Must be initialised before use; `Delete` zeroes it |
| `SRWLOCK` | 8 | Low 32 bits: exclusive bit, waiters bit, shared count | All-zero is a valid unlocked lock (static initialiser) |
| `CONDITION_VARIABLE` | 8 | Low 32 bits: wake sequence | All-zero is valid. `SleepConditionVariableSRW` releases, waits on the sequence, re-acquires |
| `SLIST_HEADER` | 16, 16-aligned | Low qword: depth in bits 0-15, sequence above; high qword: first entry | Push, pop and flush are one 128-bit compare-and-swap. Depth is where inlined `QueryDepthSList` reads it |

As built (`core/lock.c`; `rt/lock.h` is the specification):

- **`LockCount` is not the Windows 7 encoding.** It is -1 while the section is free and 0
  while it is held, whatever the number of waiters: the encoding of Windows before Server
  2003 SP1. Windows 7 keeps "free" in bit 0 and the complement of the waiter count above
  bit 1, so a held section with no waiters reads -2 there (Microsoft's debugger page "Displaying a Critical Section").
  Keeping the waiter count would cost a second atomic operation on the uncontended path;
  if guest code turns out to read the field, two constants in `lock.c` change.
- **`CONDITION_VARIABLE`: the upper 32 bits count the threads inside a sleep** on the
  variable, so that a wake with nobody to wake is one atomic load and no system call. The
  low 32 bits are the wake sequence.
- **`SRWLOCK`: a new reader waits behind a waiter.** A shared acquire waits not only for
  an exclusive holder but also whenever the waiters bit is set while the lock is held
  shared, as Windows queues a new reader behind waiters. Writers are therefore not starved
  by a stream of readers, and a thread that acquires shared a second time while a writer
  waits deadlocks, which Microsoft documents. A release wakes all waiters; who gets the
  lock next is not ordered. Releasing a lock that is not held in that mode is fatal.
- **Alignment is checked, where Windows does not check it.** The word that is waited on
  must be 4-byte aligned: a critical section and an SRW lock at an address that is not a
  multiple of 4 are fatal; a condition variable must be 8-byte aligned (its 64 bits are one
  atomic value); an `SLIST_HEADER` and every list entry must be 16-byte aligned (Windows
  raises `STATUS_DATATYPE_MISALIGNMENT` for the header).
- **`SLIST_HEADER` depth** is a 16-bit count that wraps after 65,536 pushes; emptiness is
  decided by the first-entry pointer, never by the depth.

**Why.** These are the hot paths (3,371 critical-section sites, 525 SRW sites, 434 `SList`
sites in the Respawn modules alone), so the uncontended case must be one atomic operation
on memory the guest already owns. A wait-on-address primitive lets the lock state live
entirely inside the Windows-sized structure, including the zero-initialised forms that
Windows allows without an init call. Interlocked operations are compiler intrinsics in the
guest and are already handled by the lifter.

*Rejected:* a side table from structure address to a host mutex (a lookup on every lock,
and structures are freed without notice); `pthread_mutex_t` inside the structure (does not
fit, and has no valid all-zero state for SRW locks).

## 7. Files and paths

### 7.1 A virtual `C:` drive

**Decision.** The guest sees only a small Windows file tree, mapped by a table set at
start-up:

| Windows path | Host directory |
|---|---|
| `C:\Titanfall2` | The game install root. Also the current directory at start |
| `C:\Users\Player` | `<state dir>/home` (`Documents`, `AppData\Local`, `AppData\Roaming`, `Saved Games` inside it) |
| `C:\Users\Player\AppData\Local\Temp` | `<state dir>/tmp` |
| `C:\Windows`, `C:\Windows\system32` | Exist, empty |

Anything else is `ERROR_PATH_NOT_FOUND`. `SHGetFolderPath`, `GetTempPath`,
`GetSystemDirectory` and the environment (8.2) all answer inside this tree.

**All translation goes through one core function; no shim touches a path string itself.**

```c
DWORD rt_path_to_host_w(const WCHAR *win, uint32_t flags, char host[RT_PATH_MAX]);  /* 0 or a Win32 error */
DWORD rt_path_to_host_a(const char *win, uint32_t flags, char host[RT_PATH_MAX]);   /* bytes are code page 1252 */
int   rt_path_to_win_w(const char *host, WCHAR *out, int cch);
DWORD rt_win32_from_errno(int e, RtErrContext what);
```

It accepts drive-absolute, rooted, and relative paths (against the guest's current
directory, which is runtime state, not the host's), both slashes, `.` and `..` resolved
textually as Windows does, and the `\\?\` prefix. `NUL` maps to `/dev/null`. UNC paths,
other drives and device paths fail and are logged.

**Why.** A fixed, ASCII-only Windows tree hides host paths that the game's ANSI functions
could not represent, makes runs reproducible, and stops a guest bug from writing anywhere
outside the install and state directories.

*Rejected:* exposing the host file system under a drive letter (kept as a debug switch,
off by default).

### 7.2 Case

**Decision.** At start-up the runtime asks each mapped root whether its volume is
case-sensitive. If not (the macOS default), names pass through. If it is, each path
component is resolved by exact match first and then by a case-folded lookup in a cached
directory listing, invalidated when the directory's modification time changes or when the
runtime itself creates, deletes or renames in it. Folding is ASCII only. New files keep the
caller's spelling.

**Why.** Windows file names are case-insensitive and the game relies on it. The default
macOS volume format is too, so the common case costs nothing, but a case-sensitive volume
is a supported macOS configuration and an external game drive may well be one.

*Rejected:* requiring a case-insensitive volume; lower-casing every name on disk.

### 7.3 File operations

**Decision.**

- `CreateFile` maps access and disposition to `open` flags. Sharing modes are **recorded
  and not enforced**; when Windows would have refused an open or a delete, the runtime logs
  it. `FILE_FLAG_DELETE_ON_CLOSE` unlinks at close. `FILE_FLAG_NO_BUFFERING` and
  `SEQUENTIAL_SCAN` set the matching host hints; alignment rules are not enforced.
- Attributes: directory, read-only (from permissions), otherwise archive. Times are the
  host's, as `FILETIME`.
- **Find-file** reads the whole directory at `FindFirstFile`, matches the DOS wildcards
  case-insensitively, returns `.` and `..` first, then **entries sorted by name**, with
  on-disk spelling and an empty short name.
- **Standard handles** exist and are character devices that are not consoles:
  `GetFileType` says `FILE_TYPE_CHAR` and `GetConsoleMode` fails. Every console function
  reports that no console is attached; `AllocConsole` fails.
- **Overlapped I/O.** Completion ports are not imported. What is: `ReadFile` with an
  `OVERLAPPED`, `GetOverlappedResult`, `ReadFileEx` with a completion routine, alertable
  waits. All go through one core call that, for now, **performs the I/O synchronously**,
  fills in the `OVERLAPPED`, signals its event, and for `ReadFileEx` queues the completion
  routine as an APC delivered at the thread's next alertable wait.
- **File mappings.** `CreateFileMapping` makes a section object (a duplicated descriptor,
  or an unlinked temporary file for pagefile-backed sections, so all views share pages).
  Names live in the kernel-object namespace of 4.1 and are process-local. `MapViewOfFile`
  is a shared `mmap` into a VM-arena region recorded as `MEM_MAPPED`; offsets are multiples
  of 64 KB, which host pages divide.
- Disk queries answer for the mapped root; `C:\` is the only drive, type fixed.
- Child processes (`CreateProcess`, pipes to them, `ShellExecute`) are a feature this
  machine does not have (section 9.2), decided when a caller appears.

**Why.** Sorted enumeration costs nothing and removes a real trap: NTFS returns names in
order, APFS does not, and games acquire accidental dependencies on load order.
Synchronous completion is a result Windows itself produces when data is cached, so correct
code already handles it; a worker-thread implementation can replace it behind the same core
call if streaming hitches. Enforcing sharing modes could only turn working opens into
failures; logging the would-be violations finds any place the game relies on them. The
standard handles are shaped so that the C runtime should flush after each write, as it does
for a terminal, yet send the bytes through `WriteFile` rather than its console path; the
first guest `printf` at Gate 2 will confirm or correct that.

*Rejected:* emulating a console; host directory order for enumeration; a thread pool for
overlapped I/O before anything shows it is needed.

## 8. Time, environment, registry, locale, machine

One core-owned file, `runtime/core/machine.c`, holds the description of the pretend
machine. `GetSystemInfo`, `GetVersionEx`, `VerifyVersionInfo`, `IsProcessorFeaturePresent`,
`GetLogicalProcessorInformation`, `rt_cpuid`, the process block, the environment and the
registry seed all read from it, so they cannot disagree.

### 8.1 Time

**Decision.** Two core clocks, and every time shim is arithmetic on one of them:
`rt_time_mono_100ns()` (continuous, includes sleep) and `rt_time_utc_100ns()` (wall clock in
`FILETIME` units). `QueryPerformanceFrequency` is 10,000,000. `GetTickCount`,
`GetTickCount64` and `timeGetTime` are milliseconds since runtime start plus ten minutes.
`rt_rdtsc` is the monotonic clock scaled to a nominal 3.0 GHz, the same figure the registry
seed reports. The time zone is reported as the host's current offset with no
daylight-saving rule.

**Why.** Two clocks behind one interface is where a recorded or fixed clock can be
substituted for trace comparison. A fixed-offset zone gives the same local-time conversions
Windows gives for current timestamps, without carrying zone rules.

*Rejected:* each time shim calling a host clock of its own choosing (the clocks would
disagree, and none could be replayed).

### 8.2 Environment and command line

**Decision.** The environment block is built by the runtime, not inherited: a fixed set
plus overrides. The command line is `"C:\Titanfall2\Titanfall2.exe"` followed by the
arguments given to the host program. `GetStartupInfo` reports no inherited handles.

As built (`core/env.c`; `rt/env.h` is the specification):

- **The fixed set**, seventeen variables: `COMPUTERNAME=PLAYER-PC`, `HOMEDRIVE=C:`,
  `HOMEPATH=\Users\Player`, `OS=Windows_NT`, `Path=C:\Windows\system32;C:\Windows`,
  `PATHEXT` (the Windows 7 default list), `PROCESSOR_ARCHITECTURE=AMD64`, `SystemDrive=C:`,
  `SystemRoot=C:\Windows`, `TEMP` and `TMP` (both `C:\Users\Player\AppData\Local\Temp`),
  `USERDOMAIN=PLAYER-PC`, `USERNAME=Player`, `USERPROFILE=C:\Users\Player`,
  `windir=C:\Windows`, and from the machine description (section 8.5)
  `NUMBER_OF_PROCESSORS` and `PROCESSOR_LEVEL`.
- **Overrides, in this order, a later one winning:** the embedding program's list
  `RtConfig.env_overrides` (`NAME=value` strings, so that a bundle needs no host
  variables), then every host variable named `RT_GUEST_<NAME>`, which sets `<NAME>`. An
  override with an empty value sets the empty string; nothing removes a variable.
- **A name may begin with `=`.** The C runtime's `_chdir` sets `=C:` to the new directory
  after every `SetCurrentDirectory`, and the block carries the entry as `=C:=C:\dir`. Such a
  variable is stored, returned and listed like any other; the runtime gives it no meaning
  and creates none itself (the block starts without `=C:` entries, where a Windows process
  usually inherits them; no caller is known to need that). A `=` anywhere else in a name
  is fatal, and so is a name that is not ASCII. Names compare with ASCII case folding and
  keep their first spelling.
- **`RtConfig.argv` holds the arguments without the program name**; each is quoted by the
  rules the Microsoft C runtime parses. The `A` forms of the command line and of the block
  carry ASCII only and are fatal otherwise.

**Why.** The host environment is large, differs between a terminal and a bundle launch, and
contains nothing a Windows program expects. A fixed block is reproducible and can be made
to look right.

*Rejected:* passing the host environment through.

### 8.3 Registry

**Decision.** An in-memory tree loaded from a text file in the state directory, seeded with
the handful of keys a Windows machine always has (processor description, OS version).
Writes persist to that file. A key or value that is not there returns
`ERROR_FILE_NOT_FOUND`, **logged once per name**, so the first runs tell us what the game
asks for and each addition to the seed is deliberate. Predefined roots are the Windows
constants (note that `HKEY_CURRENT_USER` is `0x80000001` sign-extended to 64 bits). Open
keys are handle-table objects.

**Why.** Nobody knows yet which keys the game reads (E1 will say). "Not found, and here is
what was asked" is both correct for an absent key and the fastest way to find out.

*Rejected:* inventing plausible values for unknown keys; failing every registry call.

### 8.4 Locale and strings

**Decision.** The machine is en-US: user and system locale `0x0409`, ANSI code page 1252,
OEM code page 437. `MultiByteToWideChar` and `WideCharToMultiByte` are our own code and
support 1252, 437, 20127, 28591 and UTF-8 (65001), with the Windows rules for invalid
input, default characters, the length-query call and `-1` lengths; other code pages fail
with `ERROR_INVALID_PARAMETER` and a log line. **Host `wchar_t`, `mbstowcs`, `iconv` and
locale functions are never used.** Character classification and case mapping are
table-driven: exact through Latin Extended-B, plus Greek and Cyrillic case pairs.
`CompareString` compares case-folded code units, which is not Windows's linguistic order.
`GetLocaleInfo`, `GetDateFormat` and `GetTimeFormat` answer from an en-US table and fail on
anything not in it.

As built (`core/locale.c`; `rt/locale.h` is the specification):

- **Which locale values mean en-US.** `0x0409`, `LOCALE_USER_DEFAULT` (`0x0400`) and
  `LOCALE_SYSTEM_DEFAULT` (`0x0800`) in every function. **Locale 0** (`LOCALE_NEUTRAL`) is
  read as the user default, as Windows reads it, in five functions: `LCMapStringW`,
  `CompareStringW`, `GetLocaleInfoW`, `GetDateFormatW` and `GetTimeFormatW` (the C
  runtime's start-up passes 0 to `LCMapStringW`). **`LOCALE_INVARIANT`** (`0x007F`) is
  taken by `LCMapStringW` and `CompareStringW` only, where simple case mapping and
  comparison give what en-US gives; its data and formats differ from en-US's, so the
  other functions do not take it. `IsValidLocale` takes neither. Any other locale value is
  the unimplemented report of section 9.2, not a failure return.
- **Best fit.** `WideCharToMultiByte` without `WC_NO_BEST_FIT_CHARS` applies Windows's
  best-fit table for code page 1252 (442 substitutions, generated from Microsoft's
  `bestfit1252.txt` by `runtime/tools/gen_bestfit.py`): U+0101 becomes `a`, U+FF21 `A`,
  U+0191 the byte `0x83`. A substitution is not counted as a use of the default character;
  a character the table lacks (U+039C, a CJK ideograph) becomes the default character and
  is counted. The C runtime's start-up needs both, converting the upper-cased 256
  characters of the page. The other single-byte pages have no table here: on them a
  character without a direct mapping, without that flag, is the unimplemented report. With
  the flag every such character becomes the default character on every page.

**Why.** Code page 1252 on an English system is the configuration the game was tested on
most; UTF-8 as the ANSI code page did not exist on the Windows version we report, and the
2012 C runtime's tables do not handle it. Most of this subsystem is a back end for the C
runtime's `setlocale`, which the game may never call with a non-default locale, so it is
built to be correct for English and to fail visibly elsewhere.

*Rejected:* UTF-8 as the ANSI code page; the host's locale and conversion functions (their
results depend on the user's macOS settings and their wide character is 32 bits).

### 8.5 The machine

**Decision.**

| Question | Answer |
|---|---|
| OS | Windows 7 SP1 x64: version 6.1, build 7601, "Service Pack 1", workstation |
| Processor | x64, family 6, one package, no SMT. Feature answers match the CPUID shim of `docs/DESIGN.md` section 7. `PF_FASTFAIL_AVAILABLE` is FALSE |
| Processor count | The host's **performance cores** (5 on this Mac), overridable with `RT_CPUS` |
| Memory | Host physical memory as total; half of it as available; 8 TB of address space |
| Debugger | `IsDebuggerPresent` is FALSE (section 9.4) |
| `EncodePointer`, `DecodePointer` | Identity |

**Why.** Windows 7 is the oldest system the game supports, so it selects the smallest set
of optional paths; the 29 probed functions may all answer NULL without contradiction. The
processor count is the lever on the engine's threading: tier0 sizes its job system from
`GetLogicalProcessorInformation`, so reporting only performance cores keeps
its workers to a number the performance cores can run, and reporting two or fewer creates
no worker threads at all, which is the earlier plan's single-threaded fallback with no code
change.

*Rejected:* reporting Windows 10 (more optional surface for no known benefit; revisit if
the renderer needs it); reporting all 11 cores.

**The CPUID answers also say what a stop means.** A lifted function may hold instructions
the lifter does not translate (`docs/DESIGN.md` section 3); lifted code that arrives at one
calls `rt_trap_untranslated(ctx, address, what, needs)` (`runtime/core/trap.c`), where `needs`
names the CPUID feature bits the instruction waits for. The report looks them up with
`rt_machine_cpuid_bit`, in the table `rt_cpuid` answers from. A feature reported absent
(everything after SSE2, so `xgetbv`, AVX, AES-NI and the rest of `engine.dll`'s gated code):
a processor answering like this one would raise #UD there, and the report says the guest
took a path its feature test should have closed. A feature reported present (MMX), or none
known: the report says the stop is the lifter's gap. Changing an answer in
`runtime/core/machine.c` therefore changes which of the two a stop is, with no other edit.

## 9. Errors

### 9.1 An import we have not implemented

**Decision.** Calling a trap stub prints one block and aborts (addresses here are made
up):

```
FATAL unimplemented import user32.dll!CreateWindowExW
  called from engine.dll+0x1f3a2c   thread 0x1a4 "Worker 2"
  rcx=... rdx=... r8=... r9=...
  guest stack: engine.dll+0x1f3a2c  engine.dll+0x1567f1  launcher.dll+0xd1a0 ...
  the stack above the home space (arguments 5 to 12, if the function takes that many):
    arg5  0000000140203f00 -> 0000b000 0000a100 ...
    arg6  0000000000000002
```

The last part is the eight words above the caller's home space: a missing import has no
prototype here, so they are printed whether or not the function takes that many arguments,
each followed by the 32 bytes it points to when it is the address of readable guest memory.

It never returns a value. The loader can print, per module, which imports are implemented,
which are policy answers and which are traps; that listing is the work queue.

**Why.** The first unimplemented call on any path stops the run at its cause, with the name
needed to open the next piece of work. Because the stub is per import, a module loads and
runs with most of its imports missing.

*Rejected:* one shared "not implemented" stub (cannot say which import); failing the load.

### 9.2 What a shim is allowed to be

Every shim file declares one status in its header, and the status decides what its body may
do:

| Status | Meaning | Rule |
|---|---|---|
| `full` | Documented behaviour, including failure returns | |
| `partial` | The listed cases work | **Every case not handled calls `RT_UNIMPLEMENTED` with the offending value.** No guessing, no default success |
| `absent` | The machine does not have this feature | Returns the failure Windows documents for a machine without it (no console, no input method, no HID devices, unknown COM class, unknown registry key) and logs. Needs the core maintainers' sign-off |
| `advisory` | Windows documents the call as a hint | May succeed without effect (`SetThreadPriority`, `timeBeginPeriod`, `SetErrorMode`, `HeapSetInformation`, `SetHandleCount`). Logs once at debug level. Core sign-off |
| `trap` | Deliberately not implemented | Body is `RT_UNIMPLEMENTED` |

There is no status for "returns success and does nothing" outside `advisory`.

**Why.** The lifter's rule is that a missing function is visible and a wrong one is not.
The same rule here: a silent stub that returns success converts a missing feature into
state corruption far from its cause. `absent` and `advisory` are the two honest ways to do
nothing, and both are narrow enough to review.

*Rejected:* stubs that log and return a default (the log is not read until after the damage
is done); a global "keep going" switch.

### 9.3 Exceptions, plan A: abort with a log

**Decision.** No guest exception handler ever runs.

- **Faults in guest code** (bad address, guest stack overflow, a lifter `TRAP`, host stack
  overflow) arrive as host signals on the alternate stack. The handler prints the signal,
  the fault address and what region it is in, the guest thread, the registers in `Ctx`,
  the host function (whose symbol name is the guest module and RVA), and a guest backtrace,
  then aborts.
- **`RaiseException`** consults a short table first. Thread-naming (`0x406D1388`) sets the
  host thread name and returns; debug-print codes print and return. Returning is what
  Windows does when a debugger continues them. The thread-naming code is in the Miles and
  Bink DLLs, so lifted middleware will raise it when it names a thread. Anything else (a
  C++ `throw`, a delay-load failure) prints the code, the decoded C++ type name or the
  delay-load DLL and function when there is one, and aborts.
- **Read-only unwinding is implemented**, because it is pure computation on guest memory
  and both the game and our own backtraces use it: `RtlCaptureContext`,
  `RtlLookupFunctionEntry` (a search of the mapped unwind table), `RtlVirtualUnwind` (one
  frame), `RtlPcToFileHeader`. They share one core-owned unwinder.
- **Transfer of control is not**: `RtlUnwindEx`, and therefore `longjmp` and every `catch`,
  traps with the target frame and address.
- `SetUnhandledExceptionFilter` stores the guest's filter and returns the previous one. On
  a fatal error the filter is **not** called unless `RT_CALL_UEF=1`.
  `UnhandledExceptionFilter`, called by the C runtime when it has already decided to die,
  prints the record it was given and aborts.

**Why.** The earlier plan chose abort-first because C++ exceptions are rare outside OriginSDK.
This section draws the line more finely than "all exceptions abort" because two things on
the wrong side of that line could stop a normal run: thread naming by middleware, and the
module and stack lookups that `dynamic_cast` and tier0's diagnostics perform. What plan B
needs is known and nothing here blocks it: resumable entry points from the lifter, host
unwinding to a recorded frame, and
`RT_CHECK_RSP` already marks every frame boundary. How much of the game depends on it is
open.

*Rejected:* dispatching host faults to guest handlers now (needs plan B's unwinding);
aborting on every `RaiseException` without looking at the code.

### 9.4 Debugger, debug output, process exit

- `IsDebuggerPresent` is FALSE and the process block agrees. TRUE would turn asserts into
  breakpoints and change what the engine does at start-up, and the reference run under
  Proton has no debugger.
- `OutputDebugStringA/W` write to the `guest.debug` log channel.
- `DebugBreak` and a guest `int3` abort with a log. `FatalAppExit` prints its message and
  exits with code 3.
- `ExitProcess` and `TerminateProcess` on the current process flush the runtime's logs and
  call `_exit` with the code. Both are **normal** paths: tier0's `Plat_ExitProcess` export
  is `TerminateProcess(GetCurrentProcess(), code)`, reached by a tail jump through the
  import slot. Detach notifications are not delivered (section 11).

### 9.5 Logging and the call trace

One function, `rt_log(channel, level, fmt, ...)`, with channels `loader`, `import`, `vm`,
`thread`, `file`, `registry`, `trap`, `guest.debug` and `trace`, selected by `RT_LOG`.
"Logged once" in this document means once per distinct key on its channel. When the `trace`
channel is on, generated thunks print one line on entry and one on return for every shim,
with arguments formatted from the `SHIM` declaration and the caller as `module+rva`,
modelled on Wine's relay log so the two can be compared after normalising addresses. That
is verification method 2 of the earlier plan.

## 10. Source layout, build, tests, "done"

### 10.1 Layout and ownership

```
runtime/
  modules.def              manifest of modules (core)
  include/rt/              shim.h wintypes.h test.h (and test_child.h test_fork.h test_pe.h), and
                           the core's headers, each the specification of its part:
                             init.h dispatch.h log.h
                             handle.h sync.h vm.h heap.h lock.h thread.h module.h
                             file.h path.h registry.h env.h locale.h time.h machine.h
                             crash.h unwind.h
                             net.h                 sockets
                             window.h gdi.h        windows, messages, device contexts, no screen
                             com.h nulldev.h       the interface layer; the do-nothing Direct3D device
  include/rt/win/          guest-visible structs and constants, one header per area (core)
  core/                    loader, dispatch, module, thread, handle, wait, sync, vm, heap, lock,
                           file, path, registry, env, locale, time, machine, process, crash,
                           exception, exit, unwind, trap, report, log, trace, init;
                           net (sockets), win (window records, classes, the message queue,
                           input, hooks, clipboard), gdi, com (the interface layer) (core)
  com/                     interface descriptions and native objects (core until Gate 4)
  nulldev/                 the do-nothing Direct3D device: objects.c, and one method per file,
                           nulldev/<Interface>/<Method>.c
  host/                    SDL3, Cocoa and Metal glue; the only place Objective-C lives (Gate 4)
  shims/<lib>/<Name>.c     one import per file (contributors)
  tests/shims/<lib>/<Name>.c   one test file per shim (contributors)
  tests/core/              tests of core (core)
  tests/nulldev/, tests/com/, tests/abi/   tests of the device's methods, of the interface
                           layer, and of the guest ABI with a test library of its own
  tools/                   gen_shims.py gen_com.py gen_imports.py gen_bestfit.py shimdef.py
                           check_shim.py check_method.py check_d3d_layout.py types.def
                           imports.def imports_extra.def (core)
  app/                     bundle, entitlements, provisioning
build/gen/                 generated thunks, shim table, module table (never committed)
```

The listing is a snapshot. Two notes on it. A core function that is declared and
specified in its header but not built yet stops with "unimplemented runtime core function";
`runtime/tests/core/unbuilt_*.c` is the list of those, and at the time of writing it holds the pieces
`win-window`, `win-keymap`, `gdi-dc` and `gdi-object` of the window contract and the socket
pieces `addr`, `opt`, `dgram`, `select` and `host`. And the bundle's rules are not under
`runtime/`: they are `app/app.mk` at the root of the repository, included by the root
`Makefile`.

**A contributor edits exactly two files: its shim and its test.** If the shim needs a type that
is not in `rt/win/`, a core function that does not exist, or a type the generator does not
know, the contributor stops and says so in the report. This is the same rule as "do not edit the
harness" in `docs/DESIGN.md`.

**Why one import per file.** A unit of work is one import, a contributor has one working copy, and
contributors do not coordinate. If every unit owns its own two files and nothing shared, any
number of them merge without conflicts and the check in 10.5 can tell exactly what a
contributor touched.

*Rejected:* one file per subsystem (every contributor in a batch would edit the same file); a
hand-maintained registration table (the same problem in one line each).

### 10.2 Naming

- `<lib>` is the DLL name in lower case without `.dll`, other punctuation as `_`
  (`kernel32`, `ws2_32`, `gfsdk_ssao_win64`).
- File: `runtime/shims/<lib>/<Name>.c`, with the export's exact spelling.
  Ordinal-only imports: `ord<number>_<name>.c` and `SHIM_ORD`.
- Symbols: `impl_<lib>_<Name>` (typed, yours), `shim_<lib>_<Name>` (generated thunk). Core
  functions are `rt_*`, core types `Rt*`, macros `RT_*`.
- An `A` function converts its strings and calls the core function its `W` sibling calls.
  A `wsock32` shim forwards to the `ws2_32` implementation. Shared logic lives in `core/`,
  never in a shim file, and a shim does not call another shim.
- Every shim file starts with:

```c
/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-readfile
 * RT-CASES:  synchronous read from a file or pipe; zero-length read; end of file
 * RT-TRAPS:  OVERLAPPED
 */
```

### 10.3 Registration and build

The build runs `gen_shims.py` over `runtime/shims/**/*.c`. From each `SHIM` line and header
it writes the thunk and one row of the shim table (library, name, ordinal, thunk, argument
classes, status). `rt_init` turns the table into native-module exports, stub addresses and
dispatch entries. Nothing else registers a shim. The runtime builds with clang as C17 with
GNU extensions, `-fno-strict-aliasing` (which the lifted code also requires), and
`-Wall -Wextra -Werror`, into `build/runtime/librt.a`.

### 10.4 Tests

```c
/* runtime/tests/shims/kernel32/CloseHandle.c */
#include "rt/test.h"

TEST(kernel32, CloseHandle)
{
    HANDLE h = (HANDLE)CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, NULL);
    T_TRUE(h != NULL);
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
    T_EQ(CALL(kernel32, CloseHandle, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}
```

`CALL` goes through the **guest ABI**, not a C call: it puts arguments in `Ctx` registers
and on a guest stack, pushes a return address, finds the shim through an import slot and
the dispatch table, and runs the generated thunk. Around every call the harness also

- fills the upper halves of 32-bit arguments, the home space and all volatile registers
  with garbage;
- plants known values in the non-volatile registers and checks them afterwards;
- checks that `rsp` advanced by exactly 8.

Tests run as ordinary command-line programs (`RT_TSO=try`), in a temporary state directory
and a temporary fake install root that the harness creates. They never touch the real
install or the user's files. Run one with `make rt-test SHIM=kernel32/CloseHandle`, all
with `make rt-test`.

A contributor's own test is a weak oracle, as an earlier review of the lifter showed. The same
test source is written so that it can also be compiled as a real Windows program, where
`CALL(kernel32, CloseHandle, h)` is simply `CloseHandle(h)`, and run under Wine on the
Ubuntu machine. If that works **(E6)**, passing under Wine becomes part of the check below.

### 10.5 What "done" means for a shim

One command, `make shim-check SHIM=<lib>/<Name>`, exit status 0. It runs
`tools/check_shim.py`, which requires all of:

1. Only the two permitted files changed.
2. The file name, the `SHIM` line and the four header tags are present and consistent, and
   the function is in list (b) of the import survey (or was added by the core).
3. Lint: no `long`, `wchar_t` or `size_t` in the signature or in guest-visible
   declarations; no direct `malloc` for memory the guest sees; no path handling outside
   `rt_path_*`; no error-code mapping outside `rt_win32_from_errno`; no include outside the
   allowed list.
4. The runtime builds with no warnings.
5. The shim's test passes through the guest ABI with the harness checks of 10.4.
6. The test has at least one case per entry in `RT-CASES` and, for `full` and `partial`, at
   least one case per failure return the documentation lists.
7. For `partial`: every entry in `RT-TRAPS` is reached by an `RT_UNIMPLEMENTED` call in the
   source.
8. The whole runtime test suite still passes (the regression gate).
9. When E6 is in place: the same test passes under Wine.

`absent` and `advisory` additionally need the core's sign-off recorded.
Families named in section 1.5 are one unit of work and pass or fail together.

## 11. Out of scope until Gate 3, and open questions

### 11.1 Deliberately not built before Gate 3

- Window, messages, input, clipboard, GDI and fonts, audio, the DXMT bridge and COM
  proxies. These bind to trap stubs. Gate 3 gets a null Direct3D device as guest-ABI native
  objects (3.7), or the materialsystem replaced as a `stubbed` module (the earlier survey suggests
  this to avoid its render thread); the runtime supports both and the choice is not made
  here.
- Exception handling beyond plan A; `longjmp`; the interpreter fallback for dispatch
  misses.
- `DLL_PROCESS_DETACH` and module unloading. Until Gate 5, exit is `_exit`; data the guest
  C runtime still holds in its own stdio buffers at exit is lost. The plan when saves and
  configuration matter: stop the other guest threads, deliver detach in reverse order on
  the exiting thread, and exit at once if that thread blocks.
- Asynchronous overlapped I/O; sharing-mode enforcement; guard pages; suspending running
  threads; thread contexts; fibers; waitable timers; completion ports.
- Networking beyond what start-up turns out to need; cryptography; child processes; the
  shell; HID and raw input; the input method editor.
- Locales other than en-US; code pages beyond those in 8.4.
- OriginSDK. tier0's three imports from it bind to trap stubs until its shim exists.
- Speed work: direct calls for import slots, cross-module direct calls, inline caches,
  skipping the float-mode switch.

### 11.2 Experiments

| | Question | Why it matters |
|---|---|---|
| E1 | Capture a Wine relay trace of the original under Proton: start-up, menu, one map load | Ground truth for which of the 739 imports are called, in what order, with what arguments; the `GetProcAddress` names, registry keys, files, COM classes and sockets. Replaces the static guesses of the import survey. **Do this first** |
| E2 | Do the two arena reservations of 1.2 succeed inside the entitled app bundle with SDL3, Metal and DXMT loaded, and do malloc zones stay below 8 TB there? (Both held in a bare command-line process.) | Decides the address map and whether heaps need their own allocator |
| E3 | Can a native arm64 process run with 4 KB pages (`posix_spawnattr_set_4k_page_size_np`), and do Metal and AppKit work in it? | Would make 5.2 exact |
| E4 | Cost of x86 ordering mode on lifted code, and of an FPCR write at every shim call | Decides 3.6 |
| E5 | Cost of one dispatch-table call; host stack bytes per guest frame; cost of the wait-on-address lock path under x86 ordering mode | Confirms section 2's cost claim and sets the host stack ratio of 6.1 |
| E6 | Cross-compile shim tests to Windows x64 and run them under the Wine that ships with Proton on the Ubuntu machine | An independent oracle for shim tests (10.4) |
| E7 | Which thread-block and process-block fields do the 17 computed `gs:` accesses and the 4 `gs:[0x30]` users read? | Confirms the field list of 6.2, in particular whether TLS slots are read inline (a TLS check is in progress) |
| E8 | Which `RaiseException` codes, `__except` filters and `longjmp` calls run on a normal boot? | Whether plan A survives to Gate 3 (a check is in progress) |
| E9 | With tier0's allocator running, how many calls reach `HeapAlloc` and `VirtualAlloc`? | Whether the Heap family is a hot path (5.4) |
| E10 | Does any module's load count reach zero before exit? | Tests the never-unload decision (1.5) |

### 11.3 Open questions that are not experiments

- **Game data on the Mac.** Gate 3 loads a map, which needs that map's pak files locally;
  the Mac has about 66 GB free and the install is 64 GB. Which subset to copy is undecided.
- **Which thread owns the window and pumps messages.** Assumed to be the first guest thread
  (6.1). Must be confirmed before Gate 4, because AppKit accepts no other.
- **Does the engine need Winsock to start a single-player game?** Other Source games open
  sockets during initialisation. If so, a small part of the Network group moves before
  Gate 3.
- **Why vstdlib calls `RtlCaptureContext` from 13 sites, and what tier0 does with
  `DeviceIoControl` at 9.** Both are on the Gate 2 list as "decide when a caller appears".
- **How the engine finds `ui*.dll` exports** . If it walks the export
  directory by hand, that works on a lifted image and would not on a native module.
- **The process-exit design** in 11.1 is a sketch; it has not been tested against how the
  engine actually shuts down.

## Appendix A. Start-up sequence

`rt_init`: reserve the arenas; build the machine description; create the process heap, the
handle table and the standard handles; build the environment, command line and registry;
read the manifest, assign bases, set every `g_base_<module>`; read each lifted module's
headers and import table to create trap stubs; build and protect the dispatch table; create
the native modules; attach the main thread as guest thread 1 (thread block, guest stack,
ordering mode, float mode).

The Gate 2 driver is then ordinary host C:

```c
rt_init(&cfg);
Ctx *ctx = rt_main_ctx();
RtModule *tier0 = rt_module_load(ctx, u"tier0.dll", 0);          /* runs its entry point */
RtModule *vstd  = rt_module_load(ctx, u"vstdlib.dll", 0);
GUESTFN f = rt_module_export(tier0, "Plat_FloatTime", 0);
double t = rt_guest_call_fp(ctx, f, NULL, 0);
```

The game itself (Gate 3) starts the way Northstar starts it: load `tier0.dll`, load
`launcher.dll`, call its `LauncherMain` export with the executable's module handle and the
command line.
