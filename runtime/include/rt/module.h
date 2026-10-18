/* The module registry, native modules, and the loader's interface
 * (docs/RUNTIME.md sections 1.2 to 1.6).
 *
 * Every module the process can load is listed in runtime/modules.def and has an RtModule
 * from rt_init on, with its base already assigned, whether or not it is ever loaded.  The
 * registry answers every "which module is this" question.  An HMODULE is the module's base.
 *
 * Three kinds (section 1.3) plus the executable:
 *   lifted   a game DLL: image built from the DLL file by the loader      [rt-core-loader]
 *   system   a Windows DLL that exists only as shims: a native module
 *   stubbed  a game DLL we chose not to lift: a native module
 *   exe      the native module standing in for Titanfall2.exe
 *
 * A native module occupies a 1 MiB slot of the image arena:
 *   +0x00000  a minimal PE header with no export directory
 *   +0x10000  shim stubs: shim i (sorted by name) is at +0x10000 + 16*i
 *   +0x40000  trap stubs and other native stubs, 16 bytes each, in creation order
 *   +0x80000  guest-visible vtables of native objects (section 3.7): 8 bytes per slot, each the
 *             address of one of this module's native stubs (rt/com.h, rt_com_register)
 * Stub bytes are filler and are never executed; a stub is an address the dispatch table
 * maps to a host function.
 *
 * Built so far: the registry, native modules, shim and trap stubs, export lookup on native
 * modules (runtime/core/module.c); for lifted modules, start-up, the image, the
 * GetProcAddress policy, export lookup, import binding, the module queries, static TLS and
 * loading itself (loader.c, loader_image.c, loader_probe.c, loader_export.c,
 * loader_import.c, loader_query.c, loader_tls.c, loader_load.c).  Not built: unloading and
 * DLL_PROCESS_DETACH (section 11.1).
 */
#ifndef RT_MODULE_H
#define RT_MODULE_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/pe.h"
#include "rt/vm.h"                  /* RT_IMAGE_ARENA_BASE, for RT_IMAGE_LATE_BASE */

struct RtThread;

/* ---- generated tables (build/gen, from runtime/tools/gen_shims.py) ----------------------- */

/* How one argument or return value crosses the guest ABI (section 3.1).  The names in
 * runtime/tools/types.def map to these. */
typedef enum {
    RT_AC_VOID = 0,   /* return only */
    RT_AC_I8, RT_AC_U8, RT_AC_I16, RT_AC_U16,   /* low bits of a register or slot; the rest is undefined */
    RT_AC_I32, RT_AC_U32,                       /* low 32 bits; the upper half is undefined */
    RT_AC_I64, RT_AC_U64,
    RT_AC_PTR,        /* a guest pointer */
    RT_AC_STR,        /* a pointer to a NUL-terminated code page 1252 string the callee only reads */
    RT_AC_WSTR,       /* the same, UTF-16 */
    RT_AC_HANDLE,
    RT_AC_GUESTFN,    /* a guest code address */
    RT_AC_F32,        /* xmm register chosen by position, or a stack slot */
    RT_AC_F64,
    RT_AC_STRUCT8,    /* a struct of 1, 2, 4 or 8 bytes, by value in the register or slot */
    RT_AC_STRUCT,     /* any other struct: argument = pointer to the caller's copy;
                         return = hidden argument 0 points at the destination */
    RT_AC_COUNT
} RtArgClass;

typedef enum { RT_ST_FULL = 1, RT_ST_PARTIAL, RT_ST_ABSENT, RT_ST_ADVISORY, RT_ST_TRAP } RtShimStatus;

/* One row of the shim table: one implemented import. */
typedef struct RtShimEntry {
    const char    *lib;       /* library as a C identifier: "kernel32", "ws2_32" */
    const char    *name;      /* the export's exact spelling: "ReadFile" */
    uint32_t       ordinal;   /* nonzero if the function is also exported by this ordinal (SHIM_ORD) */
    LiftedFn       thunk;     /* shim_<lib>_<Name>: the dispatch-level function */
    uint8_t        ret;       /* RtArgClass of the return value */
    uint8_t        nargs;     /* declared arguments (the hidden struct-return pointer not counted) */
    uint8_t        status;    /* RtShimStatus, from the RT-STATUS tag */
    uint8_t        raw;       /* 1 for SHIM_RAW: the arguments are not described */
    const uint8_t *args;      /* nargs RtArgClass values */
    const char    *file;      /* source file, relative to the repository root */
} RtShimEntry;

typedef enum { RT_MOD_LIFTED = 1, RT_MOD_SYSTEM, RT_MOD_STUBBED, RT_MOD_EXE } RtModuleKind;

/* The lifted C of one module as the lifter emits it (lifted/<module>/<module>_table.c): the
 * five symbols a host program links.  The generator writes one of these per lifted row.
 *
 * The table has a row for every function the lifter found.  A function it translated has
 * its lifted C there; one it skipped has a stub that stops the run with the reason
 * (rt_trap_skipped), so a call through a vtable slot or an export stops the same way as a
 * direct call.  `skips` lists the skipped functions, so *count - *nskips functions are
 * lifted; never report *count as that number.  rt_lifted_skip tells the two kinds of row
 * apart. */
typedef struct RtLiftedCode {
    const char        *module;    /* the lifter's module name: the prefix of the C symbols */
    uint64_t          *base;      /* &g_base_<module>: the loader stores the module's base here */
    const LiftedEntry *table;     /* <module>_table: {rva, function}, sorted by rva */
    const size_t      *count;     /* &<module>_table_count: rows, lifted and skipped */
    const LiftedSkip  *skips;     /* <module>_skips: {rva, rva of the instruction, reason}, sorted by rva */
    const size_t      *nskips;    /* &<module>_skips_count */
} RtLiftedCode;

/* The row of code->skips for the function at `rva`, or NULL: the lifter did not skip a
 * function there (it lifted one, or none starts there). */
static inline const LiftedSkip *rt_lifted_skip(const RtLiftedCode *code, uint32_t rva) {
    size_t lo = 0, hi = *code->nskips;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (code->skips[mid].rva < rva) lo = mid + 1; else hi = mid;
    }
    return lo < *code->nskips && code->skips[lo].rva == rva ? &code->skips[lo] : NULL;
}

#define RT_MODDEF_UNCHECKED  0x1u   /* test manifests only: file size, timestamp and SHA-256 are not
                                       compared (SizeOfImage always is); the loader logs a warning */
#define RT_MODDEF_LATE       0x2u   /* a lifted row written `place=late`: its base comes from the late
                                       group (below), not from the place of its row among the lifted */

/* Where the late group starts: the second half of the image arena.  The lifted modules of a
 * program get their bases first and the native modules theirs behind them, so a lifted row
 * added to a list moves the base of every native module, and a native row moves nothing but
 * comes behind everything.  A lifted module that joins a build whose addresses are in use
 * (the game's 21st DLL, which it loads by path seconds into the run) is written `place=late`:
 * late rows get their bases from here on, in the order of the list, 64 KB aligned with a
 * 64 KB gap.  No row before the group moves, and no later row of the other two kinds moves
 * the group. */
#define RT_IMAGE_LATE_BASE   (RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE / 2)

/* One row of a module table (runtime/modules.def, or a program's own list of lifted
 * modules, in the same format, passed through RtConfig.extra_modules). */
typedef struct RtModuleDef {
    RtModuleKind kind;
    const char  *file;            /* file name as on disk: "tier0.dll", "GFSDK_SSAO.win64.dll" */
    /* Identity of a lifted module's DLL, baked in by the build from the file the lifted C
     * was made from (section 1.3).  Zero for native modules.  The loader refuses a file
     * that does not match. */
    uint32_t     size_of_image;
    uint32_t     file_size;
    uint32_t     pe_timestamp;
    const char  *sha256;          /* 64 lower-case hex digits, or NULL */
    /* Lifted modules only (NULL and 0 otherwise). */
    const RtLiftedCode *code;     /* the linked lifted C; a lifted row without it is fatal in rt_init */
    const char  *dir;             /* guest directory of the DLL under C:\Titanfall2, backslashes, no
                                     leading or trailing one: "bin\\x64_retail"; "" is the install root.
                                     Stubbed modules have it too (GetModuleFileName) */
    uint32_t     flags;           /* RT_MODDEF_* */
} RtModuleDef;

extern const RtShimEntry  rt_shim_table[];
extern const unsigned     rt_shim_count;
extern const RtModuleDef  rt_module_defs[];
extern const unsigned     rt_module_def_count;

/* ---- the registry ------------------------------------------------------------------------ */

#define RT_NATIVE_SLOT        0x100000u       /* address space of one native module */
#define RT_NATIVE_SHIM_STUBS  0x010000u
#define RT_NATIVE_TRAP_STUBS  0x040000u
#define RT_NATIVE_VTABLES     0x080000u
#define RT_STUB_SIZE          16u

struct RtNativeStub;

typedef struct RtModule {
    const RtModuleDef *def;
    RtModuleKind kind;
    char      name[64];           /* file name in lower case: "kernel32.dll" */
    char      lib[64];            /* the same as a C identifier: "kernel32" (section 10.2) */
    uint64_t  base;               /* fixed at rt_init; the module's HMODULE */
    uint64_t  size;               /* SizeOfImage, or RT_NATIVE_SLOT for a native module */
    bool      native;             /* everything but RT_MOD_LIFTED */
    bool      loaded;             /* image exists.  Native modules: always.  Lifted: from step 1 of
                                     rt_module_load on, so also while its entry point runs and after
                                     a failed one; guarded by the loader lock */
    int32_t   load_count;         /* LoadLibrary minus FreeLibrary, never below 0; modules are never
                                     unloaded.  0 for a native module nobody has loaded by name and for
                                     a lifted module that is not loaded or FAILED */
    const RtShimEntry **shims;    /* native: this module's shims, sorted by name */
    unsigned  nshims;
    struct RtNativeStub *stubs;   /* native: trap stubs and other native stubs, in creation order */
    unsigned  nstubs;
    void     *lifted;             /* loader-private state of a lifted module */
} RtModule;

unsigned  rt_module_count(void);
RtModule *rt_module_at(unsigned i);                 /* manifest order, then extra modules */

/* Find a module by name with the name handling of section 1.5: take the last path
 * component (either slash), lower-case it (ASCII), append ".dll" if it has no extension.
 * Returns the module whether or not it is loaded, or NULL if the manifest has no such
 * module.  The W form returns NULL for a name with a character outside ASCII. */
RtModule *rt_module_find(const char *name);
RtModule *rt_module_find_w(const WCHAR *name);

/* The module whose address range contains `addr` (native modules included), or NULL.
 * Serves GetModuleHandleEx(FROM_ADDRESS), RtlPcToFileHeader and crash reports. */
RtModule *rt_module_from_address(uint64_t addr);

/* The executable's native module: GetModuleHandle(NULL). */
RtModule *rt_module_exe(void);

/* Write a description of a guest address for a log line: "tier0.dll+0x1a40" for an address
 * in a module, "kernel32.dll!ReadFile" for a shim, trap or native stub, "vm:0x..." for the
 * VM arena, "0x..." otherwise.  Always NUL-terminates; returns `buf`. */
char *rt_module_describe(uint64_t addr, char *buf, size_t n);

/* GetProcAddress (section 1.5).  `name` NULL means look up by `ordinal`.  On a native
 * module this searches the shim table; on a lifted module, the mapped export directory.
 * Returns base + RVA for functions and data alike.  A name the module does not have
 * returns 0 with last error ERROR_PROC_NOT_FOUND and one `import` log line per
 * (module, name); it never traps.  The names the C runtime probes kernel32.dll for are
 * answered from the policy table in runtime/core/loader_probe.c, whatever shims exist:
 * "absent" is this miss; "present" is the shim, or a trap stub until the shim is written.
 * ON A STUBBED MODULE (RT_MOD_STUBBED: a game DLL that was not lifted and not read) THERE IS
 * NO MISS: a name or ordinal with no shim STOPS THE RUN with "FATAL unimplemented import
 * GetProcAddress(<module>, <name>)" and the caller, and does not return.  "Not exported"
 * would be an answer about a DLL nobody read; for originsdk.dll, whose only shims are the
 * posted stand-ins a program hands to rt_init, it was a way around the halting stubs.
 * Takes no lock on a native module; on a lifted one, the loader lock (rt_loader_export). */
GUESTFN rt_module_export(RtModule *m, const char *name, uint32_t ordinal);

/* True if rt_module_export on `m` stops the run on a miss and never answers NULL: `m` is a
 * stubbed module.  The one place that decides it; a program that keeps a rule about such a
 * module (gate3/run.c, run_check_origin, for originsdk.dll) asks here that it is in force. */
bool rt_module_lookup_stops(const RtModule *m);

/* True if every stub of the native module `m` that is not a shim is a trap stub: the kind
 * rt_native_import makes for an import with no shim, which halts the run and names the
 * import when called.  False if rt_native_stub gave the module a stub with a function of
 * its own (the runtime's return-to-host stub in ntdll.dll is one), or if `m` is not native. */
bool rt_module_stubs_all_halt(const RtModule *m);

/* The value that goes in an import slot for (dll, name-or-ordinal) when `dll` is a native
 * module (section 1.4): the shim's stub address, or a trap stub created for this import
 * that reports it by name when called (section 9.1).  `name` NULL means by `ordinal`.
 * The same import always gets the same address.  Trap stubs are dispatch-table keys, so a
 * new one can only be created before the table is sealed, that is from the loader's
 * pre-scan in rt_init or from RtConfig.before_seal; asking for an unknown one later is
 * fatal.  Fatal also if `dll` is not a native module of the manifest. */
uint64_t rt_native_import(const char *dll, const char *name, uint32_t ordinal);

/* Create a native stub in `m`: a guest code address that dispatches to `fn`.  For the
 * runtime's own guest-visible code addresses (the return-to-host stub) and for tests.
 * `name` is used in reports and must outlive the module.  Only before the table is sealed. */
uint64_t rt_native_stub(RtModule *m, const char *name, LiftedFn fn);

/* ==== the loader for lifted modules (sections 1.4 to 1.6) =====================================
 *
 * Files: runtime/core/loader.c (start-up, the lock), loader_image.c (the image),
 * loader_probe.c (GetProcAddress policy), loader_export.c, loader_import.c, loader_query.c,
 * loader_tls.c (docs/handoff/rt-loader-pieces.md) and loader_load.c (loading, entry points,
 * the thread calls: docs/handoff/rt-loader-load.md).  All are built.  The comments here are
 * the specification.
 *
 * LAST ERROR.  Convention 1 of runtime/README.md: a function that fails sets the calling
 * thread's last error itself (rt_thread_set_last_error) and leaves it alone on success.
 * The shim returns the result and sets nothing.
 *
 * THE LOADER LOCK.  One recursive host mutex (rt_loader_lock / rt_loader_unlock).  It
 * guards every RtModule.loaded, RtModule.load_count, the load order, the loader's private
 * state and the static TLS bookkeeping.  rt_module_load, rt_module_free,
 * rt_module_disable_thread_calls, rt_module_handle*, rt_module_filename*,
 * rt_module_from_handle, rt_module_from_pc, rt_loader_export, rt_loader_thread_attach and
 * rt_loader_thread_detach take it for the length of the call.  A load never drops it in the
 * middle: no other thread ever sees a module half loaded.
 *   - It is the OUTERMOST runtime lock.  A thread that holds it may take any other runtime
 *     lock (the VM map's, the handle table's, the heap's) and may run guest code (entry
 *     points and TLS callbacks run under it, as on Windows).  A thread must NOT ask for it
 *     while it holds any other runtime lock.
 *   - Guest code that runs under it may call every loader function (the lock is recursive).
 *     It must not wait for another thread that needs the lock: that deadlocks, as it does
 *     on Windows, and nothing detects it.  A thread that an entry point creates waits in
 *     rt_loader_thread_attach until the load is over, so an entry point that waits for its
 *     new thread to do something waits for ever.
 *   - Guest code that runs under it must return.  A thread that ends inside an entry point
 *     or TLS callback (ExitThread there) would unwind past the frame that holds the lock and
 *     leave it held: that is a fatal report that names the module and the reason code, from
 *     rt_thread_exit before it unwinds and from rt_loader_thread_detach for any other way a
 *     thread ends.  (Guest thread 1 is the exception: for it ExitThread ends the process.)
 *   - Never taken from a signal handler.  rt_loader_in_entry is the one function a fatal
 *     report may call.
 *
 * A DLL FILE AND ITS LIFTED CODE.  A lifted module is a row of a module table whose `code`
 * is not NULL (RtModuleDef, above).  rt_loader_prescan looks for its DLL at
 * C:\Titanfall2\<dir>\<file> through the path layer.  A file that is not there makes the
 * module ABSENT for the life of the process (one `loader` info line): rt_module_load on it
 * fails like a DLL that is not installed.  A file that is there and is not the one the
 * lifted code was made from (size, PE timestamp, SizeOfImage, SHA-256), or is not a
 * well-formed x64 DLL, is fatal in rt_init and again whenever the file is read.
 */

/* Called by rt_init after the registry exists and before the dispatch table is sealed,
 * with no guest thread.  For every lifted module, in registry order: fatal if its row has
 * no code; set *code->base to the module's base; add its functions to the dispatch table
 * (rt_disp_add_table); reserve [base, base + size) in the VM map as RT_VM_IMAGE; read and
 * verify the DLL file (see above); for every import from a native module create the trap
 * stub if there is no shim (rt_native_import); assign the module's static TLS index if it
 * has a TLS directory.  Then apply the GetProcAddress policy table (loader_probe.c).
 * Loads nothing: no image is built and RtModule.loaded stays false. */
void rt_loader_prescan(void);

void rt_loader_lock(void);
void rt_loader_unlock(void);

/* LoadLibraryExW (sections 1.5 and 1.6).  `ctx` is the calling guest thread's.
 *   name   its last path component (after the last slash of either kind) is looked up with
 *          rt_module_find_w (ASCII lower case, ".dll" appended when there is no dot); the
 *          directory is ignored, whatever characters it has.  NULL or "" fails with
 *          ERROR_INVALID_PARAMETER (for "" not verified on Windows).
 *   flags  0, or any of LOAD_WITH_ALTERED_SEARCH_PATH and the five LOAD_LIBRARY_SEARCH_*
 *          bits, which only choose directories and are ignored.  DONT_RESOLVE_DLL_REFERENCES,
 *          LOAD_LIBRARY_AS_DATAFILE, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE,
 *          LOAD_LIBRARY_AS_IMAGE_RESOURCE and LOAD_IGNORE_CODE_AUTHZ_LEVEL end in
 *          rt_unimplemented with the flags and the name.  Any other bit fails with
 *          ERROR_INVALID_PARAMETER.
 * The checks come in this order: ctx; a flag bit outside those eleven; a NULL or empty name;
 * one of the five flags that stop; and only then is the name looked up (so the stop does not
 * depend on whether the module exists, and a NULL name fails whatever else is asked).  Then,
 * under the loader lock, with the calling thread recorded if it was not (guest thread 1):
 *   - no such module in the registry, or a lifted module that is ABSENT: NULL,
 *     ERROR_MOD_NOT_FOUND, one `loader` warning per call that names the file asked for;
 *   - a native module: load_count + 1, return it;
 *   - a lifted module that is loaded: load_count + 1, return it; the counts of the modules
 *     it imports from do not change;
 *   - a lifted module that is being loaded higher up this thread's recursion (a DllMain
 *     that loads its own module, or an import cycle): load_count + 1, return it; its entry
 *     point is not run again;
 *   - a lifted module whose earlier load FAILED: NULL, ERROR_DLL_INIT_FAILED, logged; the
 *     entry point is never run a second time (Windows unmaps and retries; we cannot).  The
 *     code is ERROR_DLL_INIT_FAILED also when the first failure was an import that is ABSENT
 *     (which gave ERROR_MOD_NOT_FOUND that first time);
 *   - otherwise load it, in this order (section 1.6):
 *       1. build the image at the base and relocate it (rt_ldr_image_map); loaded = true;
 *       2. for each import descriptor in file order: a native DLL needs nothing; a lifted
 *          DLL is loaded first by these same rules (its load_count + 1).  If that fails,
 *          this load FAILS with the same last error and one `loader` error line; the
 *          imports after it are not loaded and no slot is filled.  An imported DLL that the
 *          registry does not have is fatal (the manifest is incomplete).  Then fill every
 *          slot (rt_ldr_bind);
 *       3. static TLS: write the index, give every attached thread its block (rt_ldr_tls_load);
 *       4. protect the read-only pages (rt_ldr_image_protect);
 *       5. append the module to the load order; call its TLS callbacks, then its entry
 *          point, as fn(base, DLL_PROCESS_ATTACH, 0) through rt_guest_call, on the calling
 *          thread.  A module with no entry point (AddressOfEntryPoint 0) skips the call.
 *          The low 32 bits of rax are the BOOL; the upper half is ignored.  The third
 *          argument (lpReserved) is always 0: every load looks like a dynamic one, also the
 *          load of an import, for which Windows passes a non-NULL value.
 *       6. FALSE: the module is FAILED (it stays mapped and `loaded`, it leaves the load
 *          order, its load_count is 0, GetModuleHandle keeps finding it; not what Windows
 *          does, which unmaps); NULL, ERROR_DLL_INIT_FAILED, one `loader` error line.
 *          Modules its entry point loaded stay loaded.
 *          TRUE: load_count + 1 (1, or more if its entry point loaded the module itself),
 *          return the module.
 * The last error of a failed load is stored after the lock is released, so it replaces
 * whatever the entry points stored; a load that succeeds stores none (what the entry points
 * stored stays).
 * A fault, a trap or a dispatch miss inside an entry point or TLS callback is the process's
 * fatal report (section 9.3); rt_loader_in_entry lets the report name the module.  An
 * exception that unwinds out of DllMain is not supported (the rsp check of rt_guest_call
 * is fatal).  What Windows does with either is not verified.
 * Two threads that load the same module: the second waits for the lock and finds it loaded;
 * the entry point runs exactly once.  Fatal: ctx NULL or not the calling thread's. */
RtModule *rt_module_load(Ctx *ctx, const WCHAR *name, DWORD flags);

/* LoadLibraryExA: `name` is bytes in code page 1252.  A byte >= 0x80 anywhere in the last
 * path component cannot name a module of the registry: ERROR_MOD_NOT_FOUND, logged.
 * Everything else as rt_module_load. */
RtModule *rt_module_load_a(Ctx *ctx, const char *name, DWORD flags);

/* The module whose HMODULE is `h`: NULL means the executable; otherwise `h` must be exactly
 * the base of a module that is loaded.  Windows also accepts a data-file handle (low bits
 * set); none exists here.  Anything else: NULL with ERROR_MOD_NOT_FOUND (the code
 * FreeLibrary and GetModuleFileName report for a bad handle, and GetProcAddress's too:
 * Wine kernel32/tests/module.c testGetProcAddress_Wrong). */
RtModule *rt_module_from_handle(HMODULE h);

/* FreeLibrary.  load_count - 1, never below 0; modules are never unloaded and no entry
 * point is called.  When the count reaches 0 one `loader` info line says so; at 0 already,
 * one `loader` warning ("freed more often than loaded").  Always TRUE.  Fatal: m NULL
 * (the shim checks rt_module_from_handle first).  A native module starts at 0, so freeing
 * one that was found with GetModuleHandle and never loaded by name is that warning. */
BOOL rt_module_free(RtModule *m);

/* DisableThreadLibraryCalls.  From now on the module's entry point is not called with
 * DLL_THREAD_ATTACH and DLL_THREAD_DETACH (rt_loader_thread_attach / _detach); for good,
 * nothing turns it on again.  A module usually calls this from its own DLL_PROCESS_ATTACH,
 * while it is still loading: that works (the module is `loaded` from step 1).
 *   a native module: TRUE, nothing to do (it has no entry point);
 *   a lifted module with a TLS directory: FALSE, ERROR_MOD_NOT_FOUND, one `loader` warning,
 *       and the calls go on.  Windows documents that it "fails if the DLL has active static
 *       thread local storage"; the error code is what the NT loader's STATUS_DLL_NOT_FOUND
 *       maps to, from memory of its source, NOT verified on Windows 7;
 *   any other lifted module: TRUE.
 * Fatal: m NULL (the shim checks rt_module_from_handle first, which also gives the error
 * for a handle that is no module). */
BOOL rt_module_disable_thread_calls(RtModule *m);

/* GetModuleHandleW / A: NULL is the executable; otherwise the module that rt_module_find_w
 * (rt_module_find) gives, if it is loaded.  No count changes.  Not found or not loaded:
 * NULL with ERROR_MOD_NOT_FOUND, no log line. */
RtModule *rt_module_handle_w(const WCHAR *name);
RtModule *rt_module_handle_a(const char *name);

/* GetModuleHandleExW / A.  `name` is a WCHAR string if `wide`, else code page 1252 bytes;
 * with GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS it is an address inside the module instead
 * (NULL then finds nothing, it does not mean the executable).
 *   out NULL, a flag bit other than the three, or PIN together with UNCHANGED_REFCOUNT:
 *       FALSE, ERROR_INVALID_PARAMETER (and *out = NULL when out is not NULL);
 *   no loaded module by that name or around that address: *out = NULL, FALSE, ERROR_MOD_NOT_FOUND;
 *   found: *out = the module, TRUE.  load_count + 1 unless UNCHANGED_REFCOUNT is given.
 *       PIN changes nothing more (nothing is ever unloaded). */
BOOL rt_module_handle_ex(DWORD flags, const void *name, bool wide, RtModule **out);

/* GetModuleFileNameW / A.  The name is C:\Windows\system32\<name> for a system module,
 * C:\Titanfall2\Titanfall2.exe for the executable, C:\Titanfall2\<dir>\<file> for a lifted
 * or stubbed module (def->dir; "" is the install root), with def->file's own spelling.
 * Copies at most `cch` characters.  Returns the length without the terminator when it
 * fits with the terminator.  Too small: fills the buffer with the first cch - 1 characters
 * and a terminator, returns cch, ERROR_INSUFFICIENT_BUFFER (Windows 7; XP leaves it
 * unterminated).  cch 0: returns 0 and sets ERROR_INSUFFICIENT_BUFFER (not verified).
 * Fatal: m NULL (the shim checks rt_module_from_handle), buf NULL with cch > 0. */
DWORD rt_module_filename_w(RtModule *m, WCHAR *buf, DWORD cch);
DWORD rt_module_filename_a(RtModule *m, char *buf, DWORD cch);

/* RtlPcToFileHeader: the loaded module whose range contains `pc`, native modules included,
 * or NULL.  Never sets last error. */
RtModule *rt_module_from_pc(uint64_t pc);

/* Export lookup on a lifted module, for rt_module_export (which sets the error and logs a
 * miss).  By name (exact, case-sensitive, binary search of the sorted name table) or, when
 * `name` is NULL, by ordinal (biased by the directory's Base).  Returns base + RVA for a
 * function or data, 0 if the module has no export directory, no such name, an ordinal
 * outside [Base, Base + NumberOfFunctions) or an unused ordinal (function RVA 0).  A
 * forwarded export (its RVA lies inside the export directory) is fatal with the module,
 * the name and the forwarder string: none is expected (section 1.4).  Fatal: m not a
 * lifted module whose image is built (so it works on a module that is still loading, for
 * rt_ldr_bind); name NULL with ordinal above 0xFFFF; a malformed export directory. */
GUESTFN rt_loader_export(RtModule *m, const char *name, uint32_t ordinal);

/* ---- for the thread unit ---------------------------------------------------------------------
 * The thread unit calls these two; nothing else does.
 *
 * rt_loader_thread_attach(ctx): on the NEW thread, with its thread block, guest stack and
 * float mode ready, before its start routine.  Under the loader lock: record the thread;
 * for every loaded module that has a TLS directory allocate the thread's block (rt_galloc:
 * the template, then SizeOfZeroFill zero bytes, 16-byte aligned) and store its address at
 * [teb + RT_TEB_TLS_POINTER] + 8 * index; then, for every module of the load order, first
 * to last, call its TLS callbacks and its entry point with DLL_THREAD_ATTACH (return value
 * ignored; no entry-point call for a module after rt_module_disable_thread_calls).  The
 * modules are those in the order when the call begins: one that an entry point loads on
 * the way got DLL_PROCESS_ATTACH on this thread and gets no DLL_THREAD_ATTACH for it.
 * Calling it twice for one thread is fatal.  It waits for the loader lock, so a thread
 * that starts while another thread is inside rt_module_load runs nothing until that load
 * is over, and then sees the new module in the order.
 *
 * rt_loader_thread_detach(ctx): on the thread that ends, after its start routine returned
 * and before its guest memory is freed.  Under the loader lock: DLL_THREAD_DETACH to every
 * module of the load order, last to first (entry point, then TLS callbacks), then free the
 * thread's TLS blocks and forget the thread.  Not called for a thread that ends because
 * the process exits.  A thread that was never attached: fatal.  A thread that is still
 * marked as inside an entry point or TLS callback (it ended there): fatal, see THE LOADER
 * LOCK above.
 *
 * A thread the loader has not recorded that calls rt_module_load is recorded then, without
 * attach calls: this is how guest thread 1 works, for which the thread unit calls neither
 * function (rt/thread.h): it gets no DLL_THREAD_ATTACH and no DLL_THREAD_DETACH from any
 * module, as the first thread of a Windows process gets none from the DLLs it loads. */
void rt_loader_thread_attach(Ctx *ctx);
void rt_loader_thread_detach(Ctx *ctx);

/* For fatal reports (core/report.c, rt_report_entry): if thread `t` is inside an entry point
 * or TLS callback the loader called, store the module and the reason (DLL_PROCESS_ATTACH,
 * ...) and return true; false also for t NULL and for a thread the loader has not recorded.
 * `m` and `reason` may be NULL.  Reads one word of the thread's RT_SUB_LOADER state, which
 * holds both; takes no lock, allocates nothing, safe in a signal handler on the thread
 * itself (core/loader_tls.c says why); for another thread, only under the loader lock. */
bool rt_loader_in_entry(const struct RtThread *t, const RtModule **m, DWORD *reason);

#endif /* RT_MODULE_H */
