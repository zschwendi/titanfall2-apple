/* Declarations private to the loader's files (runtime/core/loader*.c).  Public interface and
 * specification: rt/module.h.  Nothing outside runtime/core includes this, except the
 * loader's own tests (runtime/tests/core/loader*.c, as "../../core/loader.h").
 *
 * Every function here expects the loader lock to be held by the caller, except during
 * rt_loader_prescan (rt_init is single-threaded) and where it says otherwise.  None of them
 * takes it.  "Fatal" means rt_fatal with the module's name in the message.
 *
 *   loader.c         start-up, the lock, the per-module state             built
 *   loader_image.c   the DLL file and the image                           built
 *   loader_probe.c   GetProcAddress policy for kernel32                   built
 *   loader_export.c  the export directory                                 built
 *   loader_import.c  import descriptors: trap stubs, binding the slots    built
 *   loader_tls.c     static TLS and the per-thread state                  built
 *   loader_load.c    LoadLibrary, FreeLibrary, entry points, attach calls built
 *   loader_query.c   GetModuleHandle, GetModuleFileName, RtlPcToFileHeader built
 */
#ifndef RT_CORE_LOADER_H
#define RT_CORE_LOADER_H

#include "core.h"
#include "rt/module.h"
#include "rt/path.h"
#include "rt/thread.h"
#include "rt/vm.h"

/* ---- the image (loader_image.c) -------------------------------------------------------------- */

#define RT_LDR_MAX_SECTIONS 96

typedef struct RtLdrSection {
    char     name[9];           /* NUL-terminated */
    uint32_t rva, vsize;        /* VirtualAddress, VirtualSize (SizeOfRawData if VirtualSize is 0) */
    uint32_t raw_off, raw_size; /* PointerToRawData, SizeOfRawData */
    uint32_t chars;             /* Characteristics: IMAGE_SCN_* */
} RtLdrSection;

/* A DLL laid out flat: byte `rva` of the image is bytes[rva].  Either a host buffer made by
 * rt_ldr_image_scan (bytes are as in the file: not relocated, pointers hold the preferred
 * base) or the guest image itself (bytes == (uint8_t *)m->base, relocated). */
typedef struct RtImage {
    RtModule *m;
    uint8_t  *bytes;            /* SizeOfImage bytes; zero wherever the file has nothing */
    bool      mapped;           /* true: bytes is the guest image at m->base */
    uint64_t  preferred_base;   /* ImageBase of the file */
    uint32_t  size;             /* SizeOfImage == m->size */
    uint32_t  headers_size;     /* SizeOfHeaders */
    uint32_t  entry_rva;        /* AddressOfEntryPoint; 0: none */
    uint32_t  timestamp;        /* TimeDateStamp of the file header */
    IMAGE_DATA_DIRECTORY dir[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];   /* missing ones are {0, 0} */
    unsigned  nsections;
    RtLdrSection sections[RT_LDR_MAX_SECTIONS];
} RtImage;

typedef enum {
    RT_LDR_ABSENT = 0,          /* no DLL file at start-up: can never be loaded */
    RT_LDR_UNLOADED,            /* file verified at start-up, no image yet */
    RT_LDR_LOADING,             /* between step 1 of rt_module_load and the return of its entry point; the
                                   lock is held all that time, so only the loading thread ever sees it */
    RT_LDR_LOADED,
    RT_LDR_FAILED               /* an import's load failed or the entry point returned FALSE: for good */
} RtLdrState;

/* The loader's state of one lifted module: RtModule.lifted points at it from rt_loader_prescan on. */
typedef struct RtLdrModule {
    RtModule   *m;
    const RtLiftedCode *code;
    RtLdrState  state;
    char        host_path[RT_PATH_MAX];  /* the DLL file; "" while ABSENT */
    RtImage     img;            /* the guest image; valid from rt_ldr_image_map on (img.mapped) */
    int32_t     tls_index;      /* static TLS index, assigned by rt_ldr_tls_prescan; -1: no TLS directory */
    uint32_t    order;          /* position in the load order while it is in it */
    bool        no_thread_calls;/* rt_module_disable_thread_calls: its entry point is not called with
                                   DLL_THREAD_ATTACH / DLL_THREAD_DETACH */
} RtLdrModule;

/* The state of lifted module `m`.  Fatal if `m` is NULL, native, or rt_loader_prescan has not run. */
RtLdrModule *rt_ldr_module(RtModule *m);

/* Pointer to `len` bytes at `rva` of the image, or NULL if [rva, rva + len) is not wholly
 * inside [0, img->size) (len 0 with rva <= size is a valid empty range).  Never fatal:
 * the caller says what was malformed. */
void *rt_ldr_rva(const RtImage *img, uint32_t rva, uint32_t len);

/* The NUL-terminated string at `rva`, or NULL if `rva` is outside the image or no NUL
 * follows before the image ends. */
const char *rt_ldr_cstr(const RtImage *img, uint32_t rva);

/* Read lm->m's DLL into a new host buffer, laid out flat, and fill *out.
 * Returns false, with nothing allocated, if the file does not exist (ENOENT, or the path
 * layer says ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND).  Fatal, naming the file and
 * the reason: any other failure to open or read; a file that is not the manifest's
 * (file size, PE timestamp, SHA-256 unless RT_MODDEF_UNCHECKED; SizeOfImage always);
 * a malformed image (the list is at the top of loader_image.c).
 * The caller frees the buffer with rt_ldr_image_release.  Sets lm->host_path. */
bool rt_ldr_image_scan(RtLdrModule *lm, RtImage *out);
void rt_ldr_image_release(RtImage *img);

/* Step 1 of a load: build the guest image of lm->m at m->base and fill lm->img.
 * Reads and verifies the file again (as rt_ldr_image_scan; a file that has gone is fatal
 * now), commits the headers and every section's pages read-write in the range that
 * rt_loader_prescan reserved, copies the bytes in, applies the DIR64 relocations with
 * delta = m->base - ImageBase (an image without a relocation directory is mapped as it
 * is), and stores m->base in the optional header's ImageBase field of the guest image, as
 * Windows does.  Does not touch m->loaded or lm->state.  Fatal: called twice for a module. */
void rt_ldr_image_map(RtLdrModule *lm);

/* Step 5 of section 1.6: give every 4 KB page of the mapped image its final protection in
 * the VM map (rt_vm_protect): PAGE_READONLY for the headers; per section PAGE_READWRITE if
 * IMAGE_SCN_MEM_WRITE, else PAGE_EXECUTE_READ if IMAGE_SCN_MEM_EXECUTE, else PAGE_READONLY.
 * (The VM map makes a host page writable when any guest page in it is.) */
void rt_ldr_image_protect(RtLdrModule *lm);

/* ---- GetProcAddress policy (loader_probe.c) ---------------------------------------------- */

/* End of rt_loader_prescan: for every "present" name of the table without a shim create its
 * trap stub; fatal if a family is only partly present. */
void rt_ldr_probe_prescan(void);

/* For rt_module_export on a native module: 0 = the table has nothing to say about
 * (m, name); 1 = present, *addr is the shim's stub or the trap stub; -1 = absent whatever
 * shims exist. */
int rt_ldr_probe(const RtModule *m, const char *name, GUESTFN *addr);

/* ---- imports (loader_import.c) --------------------------------------------------------------- */

/* Start-up, on the scanned (unmapped) image: walk the import descriptors; for every import
 * from a native module call rt_native_import(dll, name, ordinal), which creates the trap
 * stub when there is no shim.  Imports from lifted modules need nothing here.  Fatal: a DLL
 * name the registry does not have; a descriptor, name table, name or slot outside the
 * image; a descriptor whose OriginalFirstThunk or FirstThunk is 0.
 * Only called when img->dir[IMAGE_DIRECTORY_ENTRY_IMPORT].Size != 0. */
void rt_ldr_import_prescan(RtLdrModule *lm, const RtImage *img);

/* The modules lm imports from, in descriptor order, duplicates removed: writes at most
 * `max` RtModule pointers to `out` and returns how many the image has (0 for an image
 * without an import directory).  Two descriptors that name one module ("KERNEL32",
 * "kernel32.dll") count once.  Works on lm->img.  Fatal as rt_ldr_import_prescan, and if
 * the image is not built. */
unsigned rt_ldr_import_modules(RtLdrModule *lm, RtModule **out, unsigned max);

/* Step 3 of section 1.6, after every imported lifted module is at least LOADING: fill every
 * slot of lm->img from the import NAME table, ignoring what the file left in the slot
 * (section 1.4): native module -> rt_native_import; lifted module -> rt_loader_export,
 * and an export that does not exist is fatal ("the build is inconsistent").  A descriptor
 * whose OriginalFirstThunk is 0 is fatal (bound-only import tables are not supported).
 * Fatal too: the image is not built; an imported lifted module whose image is not built.
 * One `import` debug line per descriptor. */
void rt_ldr_bind(RtLdrModule *lm);

/* ---- static TLS and threads (loader_tls.c) ------------------------------------------------
 *
 * WHO DOES WHAT.  The thread unit makes a thread's 16 KB thread block zeroed and stores the
 * address of the array inside it (teb + RT_TEB_STATIC_TLS_ARRAY) at teb + RT_TEB_TLS_POINTER;
 * it never touches the array again.  The loader fills slot `tls_index` of that array with
 * the thread's block of each module (rt_ldr_tls_thread for the modules loaded before the
 * thread was recorded, rt_ldr_tls_load for a module loaded after) and frees the blocks when
 * the thread ends (rt_ldr_tls_thread_end).
 *
 * A BLOCK is rt_galloc(template size + SizeOfZeroFill): the template's bytes copied from the
 * MAPPED image (so pointers in it are relocated), then zeroes; 16-byte aligned.
 *
 * THE RECORDED THREADS are the loader's own list, guarded by the loader lock: a thread is
 * in it from rt_ldr_tls_thread to rt_ldr_tls_thread_end, both of which run on the thread
 * itself, and the thread block of a listed thread exists (the thread unit frees it after
 * rt_loader_thread_detach returned).  rt_ldr_tls_load writes the arrays of OTHER threads
 * while they run; that is safe because no code of the module can run on them before the
 * load returns, so nothing reads the slot yet. */

/* One recorded thread; RtThread.sub[RT_SUB_LOADER] points at it.  Fields are guarded by the
 * loader lock, except `entry` (see rt_ldr_entry_enter). */
typedef struct RtLdrThread {
    uint64_t            entry;      /* 0, or the entry point or TLS callback the thread is inside */
    RtThread           *thread;
    bool                attached;   /* rt_loader_thread_attach ran for it; false for a thread that was
                                       recorded on the way (guest thread 1: rt_ldr_tls_thread) */
    struct RtLdrThread *next;
} RtLdrThread;

/* Start-up: if img has a TLS directory give lm the next static index (0, 1, ... in registry
 * order; fatal at RT_STATIC_TLS_SLOTS), else leave lm->tls_index at -1.  The directory is
 * checked here as rt_ldr_tls_load checks it, so a malformed one is fatal in rt_init.
 * Only called when img->dir[IMAGE_DIRECTORY_ENTRY_TLS].Size != 0. */
void rt_ldr_tls_prescan(RtLdrModule *lm, const RtImage *img);

/* Step 4 of section 1.6 on the mapped image: store lm->tls_index (u32) at the address the
 * directory names and give every recorded thread its block; threads recorded later get
 * theirs from rt_ldr_tls_thread.  Blocks that exist are never moved or reallocated.
 * Fatal: lm has no TLS directory (tls_index < 0); the image is not built; called twice for
 * a module; the template, AddressOfIndex or the callback array is outside the image. */
void rt_ldr_tls_load(RtLdrModule *lm);

/* Record the calling thread with the loader if it is not recorded (RT_SUB_LOADER), with
 * blocks for every module rt_ldr_tls_load has run for.  Idempotent.  Fatal: ctx is not the
 * calling guest thread's. */
void rt_ldr_tls_thread(Ctx *ctx);

/* The end of rt_loader_thread_detach: free the calling thread's blocks, zero its array and
 * forget the thread.  Fatal: the thread is not recorded; ctx is not the calling thread's. */
void rt_ldr_tls_thread_end(Ctx *ctx);

/* The record of thread `t`, or NULL if the loader has not recorded it (also for t NULL).
 * For the calling thread no lock is needed; for another thread the loader lock must be
 * held, and the record is valid until it is released. */
RtLdrThread *rt_ldr_thread_record(const RtThread *t);

/* Number of recorded threads.  For tests and reports. */
unsigned rt_ldr_thread_count(void);

/* Call lm's TLS callbacks (0-terminated array of guest functions, each called as
 * fn(base, reason, 0), in array order, marked with rt_ldr_entry_enter); nothing if it has no
 * directory or no callbacks.  Records the calling thread if it is not recorded.  Fatal: the
 * image is not built; the array runs outside the image. */
void rt_ldr_tls_callbacks(Ctx *ctx, RtLdrModule *lm, DWORD reason);

/* What rt_loader_in_entry reports.  rt_ldr_entry_enter marks the calling thread (which must
 * be recorded) as inside an entry point or TLS callback of lm that is called with `reason`
 * (0 to 3) and returns what the thread was inside before; rt_ldr_entry_leave puts that back
 * when the guest call has returned:
 *
 *     uint64_t outer = rt_ldr_entry_enter(ctx, lm, reason);
 *     rax = rt_guest_call(ctx, fn, 3, args);
 *     rt_ldr_entry_leave(ctx, outer);
 *
 * So a DllMain that loads another module is reported as inside the inner one while that
 * runs and as inside the outer one again afterwards.  rt_ldr_entry_leave(ctx, 0) says "in
 * none": for a thread whose guest call never returned (rt_thread_exit unwinds the host
 * stack past the caller above), before its detach calls.  The mark is one word of the
 * record, stored whole by the thread itself: loader_tls.c says why a signal handler may
 * read it. */
uint64_t rt_ldr_entry_enter(Ctx *ctx, RtLdrModule *lm, DWORD reason);
void     rt_ldr_entry_leave(Ctx *ctx, uint64_t outer);

/* ---- entry points and the load order (loader_load.c) ---------------------------------------
 * RtLdrModule.state, .order and .no_thread_calls are written there and nowhere else. */

/* Call lm's entry point as fn(base, reason, 0) with rt_guest_call and return the BOOL: TRUE
 * if the low 32 bits of rax are not 0 (the upper half is ignored), TRUE without a call if
 * entry_rva is 0.  Records (module, reason) in the thread's loader state for
 * rt_loader_in_entry while the call runs, with rt_ldr_entry_enter and rt_ldr_entry_leave
 * (above), as rt_ldr_tls_callbacks does, and records the calling thread first if it is not
 * recorded.  Fatal: the image is not built; ctx is not the calling guest thread's. */
BOOL rt_ldr_call_entry(Ctx *ctx, RtLdrModule *lm, DWORD reason);

/* The load order: the lifted modules whose code has run, oldest first.  A module enters it
 * at step 5 of its load, BEFORE its TLS callbacks and its entry point run (so a module its
 * entry point loads comes after it), and leaves it again if the entry point returns FALSE;
 * so outside a load it holds exactly the modules whose entry point returned TRUE (or that
 * have none).  A module's imports are loaded before step 5 and so come before it.
 * rt_ldr_order_at: fatal for i >= rt_ldr_order_count(). */
unsigned     rt_ldr_order_count(void);
RtLdrModule *rt_ldr_order_at(unsigned i);

#endif /* RT_CORE_LOADER_H */
