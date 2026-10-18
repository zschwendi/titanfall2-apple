/* Guest virtual memory: the two arenas and the VirtualAlloc family
 * (docs/RUNTIME.md sections 1.2, 5.1, 5.2).
 *
 * Everything whose address the guest sees comes from one of two ranges reserved by
 * rt_vm_init before anything else runs, both below 8 TB:
 *
 *   image arena  0x0000_0100_0000_0000, 4 GiB    lifted modules, then native modules
 *   VM arena     0x0000_0110_0000_0000, 256 GiB  VirtualAlloc, file views, guest stacks,
 *                                                thread blocks, TLS blocks
 *
 * The guest is told pages are 4 KB and the allocation granularity is 64 KB; the host has
 * 16 KB pages.  The runtime never hard-codes the host page size: ask rt_vm_host_page().
 *
 * Built in runtime/core/vm.c (unit rt-core-vm).  The comments here are the specification;
 * the top of vm.c has the invariants and says where each rule comes from.
 *
 * THE MAP.  A *region* is one reservation inside an arena: a 64 KB aligned base, a whole
 * number of 4 KB guest pages, an RtVmType, and a side array that records for each guest
 * page whether it is reserved or committed and, if committed, its PAGE_* value exactly as
 * it was given.  A host page (16 KB) holds four guest pages and is accessible with the
 * UNION of the access of the committed guest pages in it.  What that costs: touching a
 * reserved, PAGE_NOACCESS or PAGE_GUARD page, or writing to a read-only one, does not
 * fault when a neighbour in the same host page allows the access.  rt_vm_query and the old
 * protection of rt_vm_protect are exact all the same.  Not emulated, by design: the
 * one-shot exception of PAGE_GUARD; the change from PAGE_WRITECOPY to PAGE_READWRITE at
 * the first write; execute permission (recorded, treated as read: guest bytes never run).
 *
 * WHO CALLS WHAT.
 *   - The guest's VirtualAlloc, VirtualFree, VirtualProtect and VirtualQuery, and the
 *     runtime's own private regions (thread blocks, guest stacks): rt_vm_alloc with
 *     RT_VM_PRIVATE, rt_vm_free, rt_vm_protect, rt_vm_query.
 *   - The loader (RT_VM_IMAGE) and the file unit (RT_VM_MAPPED): rt_vm_alloc with their
 *     type, and rt_vm_release to give the region back.  rt_vm_free refuses their regions,
 *     so a guest VirtualFree on a module or on a file view fails instead of removing it.
 *     FILE VIEWS have a rule of their own, stated at rt_vm_alloc.
 *   - A signal handler: rt_vm_probe only.
 *
 * LAST ERROR.  A function that fails sets the calling thread's last error itself (through
 * rt_thread_set_last_error) and says which codes; success leaves last error alone.
 *
 * LOCK.  One host lock (an os_unfair_lock) guards the region array and every side array.
 * rt_vm_alloc, rt_vm_free, rt_vm_release, rt_vm_protect and rt_vm_query take it for the
 * length of the call and take no other lock while they hold it (logging and fatal reports
 * happen after it is released), so they may be called with any other lock held.  They must
 * NOT be called from a signal handler: the interrupted thread may be the one that holds
 * the lock.  rt_vm_probe is the call for that.
 *
 * FATAL.  Any call before rt_vm_init; a host call (mprotect, mmap) that fails inside an
 * arena, because the side arrays and the host mappings would no longer agree; and the
 * cases named at each function that Windows might honour and this map does not, which end
 * in the unimplemented report (rt_unimplemented) with the offending values.
 */
#ifndef RT_VM_H
#define RT_VM_H

#include "rt/wintypes.h"
#include "rt/win/memory.h"

#define RT_IMAGE_ARENA_BASE  UINT64_C(0x0000010000000000)
#define RT_IMAGE_ARENA_SIZE  (UINT64_C(4) << 30)
#define RT_VM_ARENA_BASE     UINT64_C(0x0000011000000000)
#define RT_VM_ARENA_SIZE     (UINT64_C(256) << 30)

/* What VirtualQuery reports in MEMORY_BASIC_INFORMATION.Type for a region.  Also who owns
 * the region: only its owner's calls change it (see WHO CALLS WHAT). */
typedef enum {
    RT_VM_PRIVATE = 1,    /* MEM_PRIVATE: VirtualAlloc, stacks, thread blocks */
    RT_VM_MAPPED,         /* MEM_MAPPED: a file view (MapViewOfFile) */
    RT_VM_IMAGE           /* MEM_IMAGE: a module image; AllocationBase is the module base */
} RtVmType;

/* Reserve both arenas with no access.  The first thing rt_init does after logging is up.
 * Fatal if either range is not available: every address in the build depends on them. */
void rt_vm_init(void);

/* The host page size, asked from the kernel once by rt_vm_init (16384 on Apple silicon);
 * 0 before rt_vm_init.  Takes no lock. */
uint64_t rt_vm_host_page(void);

static inline bool rt_vm_in_image_arena(uint64_t a) { return a - RT_IMAGE_ARENA_BASE < RT_IMAGE_ARENA_SIZE; }
static inline bool rt_vm_in_vm_arena(uint64_t a)    { return a - RT_VM_ARENA_BASE < RT_VM_ARENA_SIZE; }

/* VirtualAlloc.  A *region* is one reservation: its base is 64 KB aligned and it is taken
 * first-fit from the VM arena when addr is 0, or at `addr` (rounded down to 64 KB) if that
 * whole range is free inside either arena.  Each 4 KB guest page of a region has a state
 * (reserved or committed) and a protection, kept in a side array.
 *   alloc_type  MEM_RESERVE, MEM_COMMIT or both.  MEM_COMMIT alone commits pages of an
 *               existing region (addr and size rounded outward to 4 KB); with addr 0 it
 *               means reserve and commit.  MEM_RESET succeeds and does nothing.
 *               MEM_LARGE_PAGES fails with ERROR_PRIVILEGE_NOT_HELD.  MEM_TOP_DOWN is ignored.
 *   protect     a PAGE_* value.  Execute permissions are recorded and treated as read:
 *               guest bytes are never executed.
 *   type        what VirtualQuery will say; RT_VM_IMAGE and RT_VM_MAPPED are for the loader
 *               and for file views, VirtualAlloc itself passes RT_VM_PRIVATE.
 * A host page is accessible when any guest page inside it is committed, with the union of
 * those pages' protections.  Committed memory reads as zero, also when it was decommitted
 * before; a page that is committed again while committed keeps its contents and takes the
 * new protection.  Returns the base of the allocated or committed range, or 0 with last
 * error set:
 *   ERROR_INVALID_PARAMETER   size 0; an address below 64 KB that is not 0; a range that
 *                             reaches the 8 TB limit; flags or a protection Windows 7 does
 *                             not define or does not allow together; PAGE_WRITECOPY or
 *                             PAGE_EXECUTE_WRITECOPY with RT_VM_PRIVATE
 *   ERROR_INVALID_ADDRESS     a requested range that is not wholly free and inside one
 *                             arena; a MEM_COMMIT or MEM_RESET range that is not wholly
 *                             inside one region
 *   ERROR_NOT_ENOUGH_MEMORY   no gap of the VM arena is large enough; no host memory for
 *                             the side array
 *   ERROR_PRIVILEGE_NOT_HELD  MEM_LARGE_PAGES
 * Unimplemented report: MEM_WRITE_WATCH; MEM_PHYSICAL; MEM_RESET with address 0;
 * MEM_COMMIT with one `type` inside a region of another (a guest VirtualAlloc that commits
 * inside an image or a view); MEM_RESET inside an image or a view; MEM_COMMIT or MEM_RESET
 * on host memory outside both arenas.  Fatal: a `type` that is not an RtVmType.
 * Takes the map's lock.
 *
 * FILE VIEWS (RT_VM_MAPPED), for the file unit.  The map does not know that a file is
 * mapped over a region; to it a view is a region whose pages are all committed.  So:
 *   1. Make the view with ONE call: rt_vm_alloc(addr or 0, size, MEM_RESERVE | MEM_COMMIT,
 *      protect, RT_VM_MAPPED).  Never reserve first and commit pieces afterwards, and never
 *      commit inside a view again: a commit that adds a 4 KB page to a host page which
 *      already has a committed page writes zeros over the new page, and through a shared
 *      mapping that is the file.
 *   2. Then map the file over [base, base + size rounded up to the host page) with
 *      mmap(MAP_FIXED).  The rounded range belongs to the region: no host page is shared
 *      by two regions.  The host mapping must allow every access that `protect`, and any
 *      later rt_vm_protect on the view, asks for: a refused mprotect is fatal.
 *   3. Give it back with rt_vm_release(base, RT_VM_MAPPED), which puts fresh inaccessible
 *      pages over the range and so removes the file mapping too.  Never munmap inside an
 *      arena: the kernel may hand the hole to a host allocation.
 * A module image is not mapped over: the loader reserves the image as RT_VM_IMAGE, commits
 * its sections, and copies the bytes in AFTER the commit of the pages they go to. */
uint64_t rt_vm_alloc(uint64_t addr, uint64_t size, DWORD alloc_type, DWORD protect, RtVmType type);

/* VirtualFree: for RT_VM_PRIVATE regions only.  `free_type` is exactly MEM_RELEASE or
 * exactly MEM_DECOMMIT.  MEM_RELEASE needs addr = a region base and size 0, and frees the
 * whole region whatever the state of its pages.  MEM_DECOMMIT decommits the 4 KB pages
 * that hold a byte of [addr, addr+size) (size 0 with a region base: the whole region); a
 * page that is not committed is not an error.  A host page all of whose guest pages are
 * now uncommitted is dropped, and a partly decommitted host page has those 4 KB zeroed at
 * once.  `addr` is rounded down to 4 KB before it is compared with the base (so the
 * kernel does as far as known; not verified on Windows).  TRUE, or FALSE with last error
 * and nothing changed:
 *   ERROR_INVALID_PARAMETER   a free type that is not one of the two; MEM_RELEASE with a
 *                             size; a range that reaches the 8 TB limit; a range that
 *                             runs past the end of its region; a region that is not
 *                             RT_VM_PRIVATE (an image or a view), for both free types
 *   ERROR_INVALID_ADDRESS     an address in no region; size 0 with an address that is not
 *                             the region's base
 * Unimplemented report: an address in host memory outside both arenas.  Takes the map's
 * lock. */
BOOL rt_vm_free(uint64_t addr, uint64_t size, DWORD free_type);

/* Give back the whole region whose base is exactly `base` and whose type is `type`: every
 * host page of it gets a fresh inaccessible zero-fill mapping (which also removes a file
 * that was mapped over it) and the range is free again.  This is how the loader releases
 * an image and the file unit a view; RT_VM_PRIVATE is accepted too (thread blocks, guest
 * stacks), where it does what rt_vm_free(base, 0, MEM_RELEASE) does.  No shim calls it
 * with a guest's address unless its unit has established what the address is: the type
 * check is the only thing that keeps one owner from releasing another's region.
 * TRUE, or FALSE with last error and nothing changed:
 *   ERROR_INVALID_ADDRESS     no region has this base (free space, an address inside a
 *                             region, outside both arenas, 0)
 *   ERROR_INVALID_PARAMETER   the region is of another type
 * These two are this runtime's own; the caller turns them into what its Windows call
 * reports.  Fatal: a `type` that is not an RtVmType.  Takes the map's lock. */
BOOL rt_vm_release(uint64_t base, RtVmType type);

/* VirtualProtect on committed pages of one region: update the side array, recompute the
 * union for the host pages touched, and store the exact previous protection of the first
 * page in *old_protect (which must not be NULL).  PAGE_GUARD and PAGE_NOACCESS are exact
 * only when they cover whole host pages; otherwise they are recorded and logged once.
 * Works on a region of any type.  FALSE with last error set on failure (the codes are
 * stated rule by rule in vm.c).  Takes the map's lock. */
BOOL rt_vm_protect(uint64_t addr, uint64_t size, DWORD new_protect, DWORD *old_protect);

/* VirtualQuery: describe the run of pages with the same state and protection that
 * contains `addr`, at 4 KB precision, and return sizeof(*out); 0 with
 * ERROR_INVALID_PARAMETER for an address at or above the 8 TB limit.  Free space inside an
 * arena is MEM_FREE.  For an address outside both arenas (host heap, host stack) ask the
 * kernel and report committed MEM_PRIVATE memory.  0 with ERROR_NOACCESS if `out` is NULL
 * (a buffer that is too short, ERROR_BAD_LENGTH, is the shim's to check).
 * TAKES THE MAP'S LOCK for an address inside an arena, and makes a Mach call for one
 * outside: never call it from a signal handler (rt_vm_probe). */
SIZE_T rt_vm_query(uint64_t addr, MEMORY_BASIC_INFORMATION *out);

/* ---- the query for a signal handler and for fatal reports (review finding R14) ------------- */

typedef enum {
    RT_VM_PROBE_OUTSIDE = 0,  /* `addr` is in neither arena: host memory, or nothing.  The map knows
                                 nothing about it and the kernel is not asked */
    RT_VM_PROBE_BUSY,         /* in an arena, but the map's lock is held, perhaps by the very thread
                                 that is asking (a fault inside the map): no answer can be read */
    RT_VM_PROBE_FREE,         /* in an arena, in no region */
    RT_VM_PROBE_RESERVED,     /* in a region; this guest page is not committed */
    RT_VM_PROBE_COMMITTED     /* in a region; this guest page is committed */
} RtVmProbeState;

typedef struct {
    RtVmProbeState state;
    uint64_t base;            /* RESERVED, COMMITTED: the region's base (VirtualQuery's AllocationBase) */
    uint64_t size;            /* RESERVED, COMMITTED: the whole region's size in bytes */
    DWORD    protect;         /* COMMITTED: the page's PAGE_* value exactly as recorded; else 0 */
    RtVmType type;            /* RESERVED, COMMITTED: the region's type; else 0 */
} RtVmProbe;

/* What the map says about the guest page that contains `addr`, WITHOUT WAITING FOR THE
 * MAP'S LOCK: the lock is tried once (os_unfair_lock_trylock, which fails, and does not
 * abort, when the caller already owns the lock) and released before returning.  Makes no
 * Mach call, allocates nothing, never logs, never sets last error, is never fatal.  It is
 * the one function of this header that a signal handler may call, and the one a fatal
 * report uses (rt/crash.h).
 *   out   receives the answer; every field that the state does not define is 0.  May be
 *         NULL when only the state is wanted.
 * Returns the state, which is also out->state.  Before rt_vm_init there are no regions:
 * the answer is OUTSIDE or FREE by the address alone.
 * RT_VM_PROBE_BUSY is a real answer that every caller must print or act on as "unknown";
 * retrying in a loop from a signal handler would wait for a thread that may be the caller. */
RtVmProbeState rt_vm_probe(uint64_t addr, RtVmProbe *out);

#endif /* RT_VM_H */
