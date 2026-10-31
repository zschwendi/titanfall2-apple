/* Guest virtual memory: the map behind VirtualAlloc, VirtualFree, VirtualProtect and
 * VirtualQuery (docs/RUNTIME.md sections 1.2, 5.1 and 5.2; interface: rt/vm.h).
 *
 * THE MAP.  Two arenas are reserved with no access before anything else runs.  A *region*
 * is one reservation inside an arena: its base is 64 KB aligned, its size is a whole number
 * of 4 KB guest pages, and it owns a side array with one entry per guest page:
 *
 *     0            the page is reserved, not committed
 *     otherwise    the page is committed, and the entry is its PAGE_* protection exactly as
 *                  the guest gave it (base value and modifier, for example PAGE_READWRITE |
 *                  PAGE_GUARD)
 *
 * A guest page that is in no region is free.  Regions are kept in an array sorted by base.
 *
 * HOST PAGES.  The host page (16 KB on Apple silicon, asked from the kernel, never assumed)
 * holds several guest pages.  Region bases are 64 KB aligned and the host page size divides
 * 64 KB, so no host page is ever shared by two regions.  Three invariants hold whenever
 * the lock is not held:
 *
 *   (1) A host page inside a region is accessible with the UNION of the access of the
 *       committed guest pages it contains (host_access below says what each PAGE_* value
 *       contributes).  A host page with no committed guest page has no access.
 *   (2) A host page with no committed guest page holds no memory: it is a fresh
 *       zero-fill mapping, so it reads as zero when it is next made accessible.
 *   (3) In a host page that does have committed guest pages, every guest page that is not
 *       committed holds zeros: decommit zeroes it at once.  (The guest can break this by
 *       writing through the union into a neighbour it never committed, which would fault
 *       on Windows; commit therefore zeroes such a page again, so "commit yields zeros"
 *       holds whatever the guest did.)
 *
 * What the union costs (section 5.2): an access to a reserved page, a PAGE_NOACCESS page, a
 * PAGE_GUARD page or a write to a read-only page does not fault when a neighbour in the same
 * host page allows it.  The side array still records the exact value, so VirtualQuery and
 * the old protection of VirtualProtect are exact.
 *
 * NOT EMULATED, by design: PAGE_GUARD's one-shot exception (a guard page that fills its
 * host pages simply has no access; no handler ever clears the flag); the change from
 * PAGE_WRITECOPY to PAGE_READWRITE when an image or view page is first written (the value
 * given is the value reported); execute permission (recorded, treated as read).
 *
 * LOCKING.  One lock guards the region array and every side array.  It is held for the
 * length of one call, never across a call out of this file other than to the host kernel
 * and the C library, and never across a guest call (this file makes none).  Logging and
 * every fatal report happen after it is released.  It is an os_unfair_lock, not a pthread
 * mutex: under contention a pthread mutex hands itself from waiter to waiter, each of
 * which has to be scheduled first, and eight threads calling into the map then spend
 * milliseconds per call on a busy machine (measured: a test of 160,000 calls did not
 * finish in two minutes).  The unfair lock lets a running thread take it.
 *
 * WHERE THE RULES COME FROM.  A comment that says "Rule:" and quotes is the Microsoft
 * documentation of the call.  Where the documentation is silent (which error code, in which
 * order, what happens to an address that is not page aligned) the rule is what the NT kernel
 * does as far as it could be established without a Windows machine: from the ReactOS
 * reimplementation of the kernel's virtual memory calls, from Wine's table of NTSTATUS to
 * Win32 error codes, and from the checks Wine's test suite makes against Windows.  Those
 * are marked "kernel:".  What could not be established is marked UNSURE.
 *
 * FAILURES.  A Win32 failure sets last error and returns 0 or FALSE; success leaves last
 * error alone (section 3.4).  A call that Windows might honour and this map does not
 * handle ends in the unimplemented report with the offending values (section 9.2); the
 * cases are listed at vm_unhandled.  A host call that fails inside an arena is fatal: the
 * side arrays and the host mappings would no longer agree.
 */
#include <errno.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <os/lock.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core.h"
#include "rt/vm.h"

#define GPAGE     UINT64_C(0x1000)          /* the guest page (section 5.1) */
#define GRAN      UINT64_C(0x10000)         /* the allocation granularity (section 5.1) */
_Static_assert(GPAGE == RT_GUEST_PAGE && GRAN == RT_GUEST_GRANULARITY, "rt/win/memory.h disagrees");

/* One past the highest address the guest is told exists: section 5.1 gives the range
 * 0x10000 to 0x7FFFFFEFFFF, the Windows 7 limit.  Windows refuses a range that reaches
 * past its limit with ERROR_INVALID_PARAMETER before it looks at the address space.
 * NOT MODELLED: the kernel (as ReactOS has it) compares VirtualAlloc ranges against a limit
 * 64 KB lower, its "highest VAD address" 0x7FFFFFDFFFF, compares VirtualFree and
 * VirtualProtect against this one, and has VirtualQuery describe the 64 KB in between as a
 * reserved no-access region.  Here one limit serves all four calls and those 64 KB are
 * host address space like any other: VirtualQuery asks the host kernel about them (they
 * are normally free), and VirtualAlloc there fails with ERROR_INVALID_ADDRESS where
 * Windows says ERROR_INVALID_PARAMETER.  Nothing can be allocated there either way. */
#define USER_END  UINT64_C(0x7FFFFFF0000)

/* A valid Windows 7 allocation type that rt/win/memory.h does not define: address
 * windowing extensions.  Named here only so that it can be refused by name. */
#define VM_MEM_PHYSICAL  0x00400000u

#define PAGE_BASE_MASK   0xFFu
#define PAGE_MODIFIERS   (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)
#define PAGE_ANY_WRITECOPY  (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)

typedef struct {
    uint64_t  base;             /* 64 KB aligned */
    uint64_t  npages;           /* guest pages; the region is [base, base + npages * 4 KB) */
    uint64_t  hi;               /* every page with index >= hi is uncommitted (a high-water
                                   mark of commits, so that scans of a large reservation of
                                   which little was ever committed stay short) */
    uint16_t *pg;               /* the side array: one entry per guest page, 0 or a PAGE_*
                                   value.  It is padded to a whole number of host pages; the
                                   padding stands for the free pages between the end of the
                                   region and the end of its last host page and stays 0 */
    DWORD     alloc_protect;    /* the protection given when the region was reserved */
    RtVmType  type;
} Region;

static uint64_t host_page;
static Region  *regions;                    /* sorted by base */
static size_t   nregions, cap_regions;
static bool     warned_guard, warned_noaccess;      /* "logged once" (section 5.2) */
static os_unfair_lock vm_lock = OS_UNFAIR_LOCK_INIT;

static uint64_t up(uint64_t v, uint64_t a)   { return (v + a - 1) & ~(a - 1); }
static uint64_t down(uint64_t v, uint64_t a) { return v & ~(a - 1); }
static uint64_t region_end(const Region *r)  { return r->base + r->npages * GPAGE; }

/* A broken invariant found while the lock is held: release it and stop. */
#define VM_DIE(...)  do { os_unfair_lock_unlock(&vm_lock); rt_fatal(__VA_ARGS__); } while (0)

/* The unimplemented report of section 9.2, from a core function that has no ctx. */
static Ctx *self_ctx(void) { RtThread *t = rt_thread_self(); return t ? &t->ctx : NULL; }
#define VM_UNIMPLEMENTED(...)  rt_unimplemented(self_ctx(), __FILE__, __LINE__, __VA_ARGS__)

static const char *type_name(RtVmType t) {
    return t == RT_VM_IMAGE ? "image" : t == RT_VM_MAPPED ? "mapped" : "private";
}

/* ---- start-up ---------------------------------------------------------------------------- */

static void reserve_arena(uint64_t base, uint64_t size, const char *what) {
#if defined(RT_VM_LAYOUT_IOS) && RT_VM_LAYOUT_IOS
    _Static_assert(sizeof(vm_address_t) == sizeof(uint64_t), "64-bit VM API required");
    vm_address_t address = (vm_address_t)base;
    /* FIXED alone refuses collisions. Never use VM_FLAGS_OVERWRITE or mmap's
     * MAP_FIXED to acquire an arena: they could destroy an unrelated mapping. */
    kern_return_t kr = vm_map(mach_task_self(), &address, (vm_size_t)size, 0,
        VM_FLAGS_FIXED, MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE,
        VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS || address != base)
        rt_fatal("cannot reserve the iOS %s (%llu GiB at %#llx), Mach error %d; "
                 "Extended Virtual Addressing is required and the range must be free",
                 what, (unsigned long long)(size >> 30), (unsigned long long)base, kr);
#else
    void *p = mmap((void *)(uintptr_t)base, size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        rt_fatal("cannot reserve the %s (%llu GiB at %#llx)", what, (unsigned long long)(size >> 30),
                 (unsigned long long)base);
    if ((uint64_t)(uintptr_t)p != base)
        rt_fatal("the %s must be at %#llx but that range is taken (the kernel offered %p); every guest "
                 "address of the build depends on it (docs/RUNTIME.md section 1.2)",
                 what, (unsigned long long)base, p);
#endif
}

void rt_vm_init(void) {
    if (host_page) rt_fatal("rt_vm_init called twice");
    uint64_t hp = (uint64_t)sysconf(_SC_PAGESIZE);
    /* The map needs whole guest pages per host page, and host pages that divide the 64 KB
     * granularity so that two regions never share one. */
    if (hp < GPAGE || (hp & (hp - 1)) || GRAN % hp)
        rt_fatal("unsupported host page size %llu", (unsigned long long)hp);
    reserve_arena(RT_IMAGE_ARENA_BASE, RT_IMAGE_ARENA_SIZE, "image arena");
    reserve_arena(RT_VM_ARENA_BASE, RT_VM_ARENA_SIZE, "VM arena");
    host_page = hp;
}

uint64_t rt_vm_host_page(void) { return host_page; }

/* ---- the region array (call with the lock held) -------------------------------------------- */

/* Index of the first region whose base is above addr; nregions if there is none. */
static size_t first_region_above(uint64_t addr) {
    size_t lo = 0, hi = nregions;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (regions[mid].base <= addr) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* The region that contains addr, or NULL. */
static Region *find_region(uint64_t addr) {
    size_t i = first_region_above(addr);
    if (i == 0) return NULL;
    Region *r = &regions[i - 1];
    return addr < region_end(r) ? r : NULL;
}

/* True if no region overlaps [base, end). */
static bool range_free(uint64_t base, uint64_t end) {
    size_t i = first_region_above(base);
    if (i > 0 && region_end(&regions[i - 1]) > base) return false;
    if (i < nregions && regions[i].base < end) return false;
    return true;
}

/* The lowest 64 KB aligned address in the VM arena at which `bytes` fit (first fit), or 0. */
static uint64_t first_fit(uint64_t bytes) {
    const uint64_t arena_end = RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE;
    uint64_t cand = RT_VM_ARENA_BASE;
    size_t i = RT_VM_ARENA_BASE ? first_region_above(RT_VM_ARENA_BASE - 1) : 0;
    for (; i < nregions; i++) {
        if (regions[i].base - cand >= bytes) break;         /* the gap below this region is big enough */
        cand = up(region_end(&regions[i]), GRAN);           /* the slack up to the next 64 KB is unusable */
    }
    if (cand >= arena_end || arena_end - cand < bytes) return 0;
    return cand;
}

static bool region_insert(const Region *nr) {
    if (nregions == cap_regions) {
        size_t cap = cap_regions ? cap_regions * 2 : 64;
        Region *grown = realloc(regions, cap * sizeof *grown);
        if (!grown) return false;
        regions = grown;
        cap_regions = cap;
    }
    size_t i = first_region_above(nr->base);
    memmove(&regions[i + 1], &regions[i], (nregions - i) * sizeof *regions);
    regions[i] = *nr;
    nregions++;
    return true;
}

/* ---- protections ----------------------------------------------------------------------------- */

/* Windows rule (the memory protection constants page; the same check serves VirtualAlloc
 * and VirtualProtect): a protection is exactly one of the eight base values, optionally with
 * ONE of PAGE_GUARD, PAGE_NOCACHE and PAGE_WRITECOMBINE; none of the three may be combined
 * with PAGE_NOACCESS or with each other; any other bit is invalid.  (Bits that later
 * Windows versions define, such as PAGE_TARGETS_INVALID, are invalid on the Windows 7 this
 * runtime reports.)  An invalid value fails with ERROR_INVALID_PARAMETER. */
static bool protect_valid(DWORD p) {
    DWORD base = p & PAGE_BASE_MASK, mod = p & ~PAGE_BASE_MASK;
    if (mod & ~PAGE_MODIFIERS) return false;
    if (base == 0 || (base & (base - 1))) return false;
    if (mod & (mod - 1)) return false;
    if (mod && base == PAGE_NOACCESS) return false;
    return true;
}

/* What a committed guest page with recorded protection `rec` contributes to its host page.
 * Execute permission is recorded and treated as read: guest bytes are never executed
 * (section 5.2).  A copy-on-write page is private to this process already, so it is simply
 * writable.  PAGE_GUARD and PAGE_NOACCESS contribute nothing: they are exact when they
 * (with uncommitted pages) fill the host page, and otherwise the neighbours' access wins.
 * PAGE_NOCACHE and PAGE_WRITECOMBINE mean nothing here. */
static int host_access(uint16_t rec) {
    if (rec & PAGE_GUARD) return PROT_NONE;
    switch (rec & PAGE_BASE_MASK) {
    case PAGE_NOACCESS:          return PROT_NONE;
    case PAGE_READONLY:
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:      return PROT_READ;
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE;
    default:                     return -1;         /* never stored: protect_valid did not let it through */
    }
}

/* ---- host pages (call with the lock held) ------------------------------------------------ */

static void host_protect(uint64_t addr, uint64_t len, int prot) {
    if (mprotect((void *)(uintptr_t)addr, len, prot) != 0) {
        int e = errno;
        VM_DIE("mprotect(%#llx, %#llx, %d) failed inside an arena: %s", (unsigned long long)addr,
               (unsigned long long)len, prot, strerror(e));
    }
}

/* Give the memory of whole host pages back: map fresh inaccessible zero-fill pages over
 * them.  (madvise does not promise zeros on this system; a new mapping does.) */
static void host_drop(uint64_t addr, uint64_t len) {
    void *p = mmap((void *)(uintptr_t)addr, len, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        int e = errno;
        VM_DIE("cannot return %#llx bytes at %#llx to the arena: %s", (unsigned long long)len,
               (unsigned long long)addr, strerror(e));
    }
}

/* Adjacent host pages that need the same host call are done in one. */
typedef struct { uint64_t start, end; int prot; bool drop, active; } Batch;

static void batch_flush(Batch *b) {
    if (!b->active) return;
    if (b->drop) host_drop(b->start, b->end - b->start);
    else host_protect(b->start, b->end - b->start, b->prot);
    b->active = false;
}

static void batch_add(Batch *b, uint64_t addr, uint64_t len, int prot, bool drop) {
    if (b->active && b->end == addr && b->prot == prot && b->drop == drop) { b->end += len; return; }
    batch_flush(b);
    *b = (Batch){ addr, addr + len, prot, drop, true };
}

/* Where PAGE_GUARD or PAGE_NOACCESS was found sharing a host page with accessible pages
 * for the first time in this process (0: not found, or reported before). */
typedef struct { uint64_t guard_at, noaccess_at; } Inexact;

typedef enum { OP_COMMIT, OP_DECOMMIT, OP_PROTECT } Op;

/* Change guest pages [p0, p1) of region r (page indices, p0 < p1 <= r->npages) and bring
 * the host pages they lie in back to the invariants at the top of this file.
 *   OP_COMMIT    every page becomes committed with protection `rec`; a page that was
 *                committed already keeps its contents
 *   OP_PROTECT   every page (all committed: the caller checked) gets protection `rec`
 *   OP_DECOMMIT  every page becomes reserved
 * Other threads may be using committed neighbours in the same host pages while this runs.
 * A host page therefore only ever goes from its old union to a superset of it (to write
 * zeros) and then to its new union, never through less access than a page that stays
 * committed is entitled to; and a host page is dropped only when nothing in it is committed
 * any more. */
static void apply(Region *r, uint64_t p0, uint64_t p1, Op op, uint16_t rec, Inexact *ix) {
    const uint64_t per_host = host_page / GPAGE;
    Batch b = { 0 };

    for (uint64_t h = p0 / per_host; h * per_host < p1; h++) {
        /* The guest pages of this host page are [g0, g1).  In the last host page of a
         * region g1 may lie past the region's end: those side array entries are padding
         * and stay 0, and [lo, hi), the part this call changes, ends at p1 <= npages. */
        uint64_t g0 = h * per_host, g1 = g0 + per_host;
        uint64_t lo = p0 > g0 ? p0 : g0, hi = p1 < g1 ? p1 : g1;
        uint64_t haddr = r->base + g0 * GPAGE;

        int  old_union = PROT_NONE;
        bool had = false;                                           /* a committed page before the change */
        for (uint64_t g = g0; g < g1; g++) {
            if (!r->pg[g]) continue;
            int a = host_access(r->pg[g]);
            if (a < 0) VM_DIE("the side array of the region at %#llx holds %#x for page %llu",
                              (unsigned long long)r->base, r->pg[g], (unsigned long long)g);
            had = true;
            old_union |= a;
        }

        uint32_t zero = 0;                                          /* bit i: guest page g0 + i gets zeroed */
        for (uint64_t g = lo; g < hi; g++) {
            switch (op) {
            case OP_COMMIT:
                /* A newly committed page in a host page that was already in use: invariant
                 * (3) says it is zero unless the guest wrote there through the union. */
                if (!r->pg[g] && had) zero |= 1u << (g - g0);
                r->pg[g] = rec;
                break;
            case OP_PROTECT:
                r->pg[g] = rec;
                break;
            case OP_DECOMMIT:
                if (r->pg[g]) zero |= 1u << (g - g0);
                r->pg[g] = 0;
                break;
            }
        }

        int  new_union = PROT_NONE;
        bool has = false, has_guard = false, has_noaccess = false;
        for (uint64_t g = g0; g < g1; g++) {
            if (!r->pg[g]) continue;
            int a = host_access(r->pg[g]);              /* valid: checked above, or `rec` */
            has = true;
            new_union |= a;
            if (r->pg[g] & PAGE_GUARD) has_guard = true;
            else if (a == PROT_NONE) has_noaccess = true;
        }

        if (!has) {
            /* Decommit emptied the host page: drop it (invariant 2).  Nothing to zero. */
            if (had) batch_add(&b, haddr, host_page, PROT_NONE, true);
            continue;
        }

        int cur = old_union;
        if (zero) {
            if (!(cur & PROT_WRITE)) {
                host_protect(haddr, host_page, PROT_READ | PROT_WRITE);
                cur = PROT_READ | PROT_WRITE;
            }
            for (uint64_t g = g0; g < g1; g++)
                if (zero & (1u << (g - g0))) memset((void *)(uintptr_t)(r->base + g * GPAGE), 0, GPAGE);
        }
        if (cur != new_union) batch_add(&b, haddr, host_page, new_union, false);

        /* Section 5.2: PAGE_GUARD and PAGE_NOACCESS are exact only when they cover whole
         * host pages; otherwise they are recorded and logged once. */
        if (new_union != PROT_NONE) {
            if (has_guard && !warned_guard) { warned_guard = true; ix->guard_at = haddr; }
            if (has_noaccess && !warned_noaccess) { warned_noaccess = true; ix->noaccess_at = haddr; }
        }
    }
    batch_flush(&b);
}

static void report_inexact(const Inexact *ix) {
    if (ix->guard_at)
        rt_log_once(RT_CH_VM, RT_LL_WARN, "PAGE_GUARD inexact",
                    "PAGE_GUARD covers only part of the %llu KB host page at %#llx, which other committed "
                    "pages keep accessible: the protection is recorded and VirtualQuery reports it, but an "
                    "access will not fault (docs/RUNTIME.md section 5.2; logged once)",
                    (unsigned long long)(host_page >> 10), (unsigned long long)ix->guard_at);
    if (ix->noaccess_at)
        rt_log_once(RT_CH_VM, RT_LL_WARN, "PAGE_NOACCESS inexact",
                    "PAGE_NOACCESS covers only part of the %llu KB host page at %#llx, which other "
                    "committed pages keep accessible: the protection is recorded and VirtualQuery reports "
                    "it, but an access will not fault (docs/RUNTIME.md section 5.2; logged once)",
                    (unsigned long long)(host_page >> 10), (unsigned long long)ix->noaccess_at);
}

/* ---- memory that is not the guest's ---------------------------------------------------------- */

static bool in_arena(uint64_t a) { return rt_vm_in_image_arena(a) || rt_vm_in_vm_arena(a); }

/* Ask the kernel for the host mapping that contains `addr` or, failing that, the next one
 * above it.  False if there is none at or above addr. */
static bool host_region(uint64_t addr, uint64_t *start, uint64_t *end, int *prot) {
    vm_address_t a = addr;
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = vm_region_64(mach_task_self(), &a, &size, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    if (kr == KERN_INVALID_ADDRESS) return false;
    if (kr != KERN_SUCCESS) rt_fatal("vm_region_64(%#llx) failed: kern_return %d", (unsigned long long)addr, kr);
    *start = a;
    *end = a + size;
    *prot = info.protection;
    return true;
}

/* True if `addr` is memory of the host process outside both arenas: the host heap, a host
 * stack, the program itself.  VirtualQuery describes such memory as committed and private
 * (section 5.2), so the guest may go on to protect, commit or free it; Windows would do
 * that, this map cannot, and saying "invalid address" would contradict what VirtualQuery
 * just said.  Those calls end in the unimplemented report instead (vm_unhandled).  The
 * first 64 KB never count: Windows maps nothing there, so a null or near-null pointer gets
 * the ordinary Win32 error. */
static bool is_host_memory(uint64_t addr) {
    uint64_t start, end;
    int prot;
    if (addr < GRAN || in_arena(addr)) return false;
    return host_region(addr, &start, &end, &prot) && start <= addr;
}

/* ---- calls this map does not handle (section 9.2) -------------------------------------------- */

typedef enum {
    UNHANDLED_NONE = 0,
    UNHANDLED_HOST_MEMORY,      /* commit, reset, free or protect of host memory outside the arenas */
    UNHANDLED_COMMIT_TYPE,      /* MEM_COMMIT with one RtVmType inside a region of another */
    UNHANDLED_RESET_TYPE,       /* MEM_RESET inside an image or a file view */
} Unhandled;

/* Every call that ends in the unimplemented report rather than in a Win32 error:
 *   - MEM_WRITE_WATCH and MEM_PHYSICAL (rt_vm_alloc): valid on Windows 7, need machinery
 *     that does not exist here (write tracking; address windowing).
 *   - MEM_RESET with address 0 (rt_vm_alloc).  UNSURE what Windows does with it.
 *   - MEM_COMMIT, MEM_RESET, VirtualFree or VirtualProtect on host memory outside both
 *     arenas (see is_host_memory).
 *   - MEM_COMMIT with one type inside a region of another type: a guest VirtualAlloc
 *     (RT_VM_PRIVATE) that commits pages of a file view or of a module image.  Windows
 *     allows it for some views (those of a section created with SEC_RESERVE) and refuses
 *     it for others.  UNSURE with which error, and only the unit that creates views knows
 *     which kind a view is.
 *   - MEM_RESET inside an image or a view.  Windows documents that it fails for memory
 *     mapped to a file; UNSURE with which error. */
static _Noreturn void vm_unhandled(Unhandled why, const char *call, uint64_t addr, uint64_t size, DWORD flags,
                                   RtVmType asked, RtVmType found) {
    switch (why) {
    case UNHANDLED_HOST_MEMORY:
        VM_UNIMPLEMENTED("%s on host memory outside both arenas (address %#llx, size %#llx, flags %#x): "
                         "VirtualQuery reports such memory as committed, but this map cannot change it",
                         call, (unsigned long long)addr, (unsigned long long)size, flags);
    case UNHANDLED_COMMIT_TYPE:
        VM_UNIMPLEMENTED("%s: MEM_COMMIT with type %s inside a region of type %s (address %#llx, size %#llx)",
                         call, type_name(asked), type_name(found), (unsigned long long)addr,
                         (unsigned long long)size);
    case UNHANDLED_RESET_TYPE:
        VM_UNIMPLEMENTED("%s inside a region of type %s (address %#llx, size %#llx)",
                         call, type_name(found), (unsigned long long)addr, (unsigned long long)size);
    case UNHANDLED_NONE:
        break;
    }
    rt_fatal("vm_unhandled(%d)", (int)why);
}

/* ---- VirtualAlloc ---------------------------------------------------------------------------- */

static uint64_t alloc_fail(DWORD err, uint64_t addr, uint64_t size, DWORD alloc_type, DWORD protect) {
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "alloc(%#llx, %#llx, type %#x, protect %#x) failed: error %u",
               (unsigned long long)addr, (unsigned long long)size, alloc_type, protect, err);
    rt_thread_set_last_error(err);
    return 0;
}

uint64_t rt_vm_alloc(uint64_t addr, uint64_t size, DWORD alloc_type, DWORD protect, RtVmType type) {
    if (!host_page) rt_fatal("rt_vm_alloc before rt_vm_init");
    if (type != RT_VM_PRIVATE && type != RT_VM_MAPPED && type != RT_VM_IMAGE)
        rt_fatal("rt_vm_alloc: %d is not an RtVmType", (int)type);

    /* Rule (kernel32, before the kernel is asked): a non-null address inside the first
     * 64 KB fails with ERROR_INVALID_PARAMETER. */
    if (addr != 0 && addr < GRAN)
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);

    /* Rule: an allocation type with a bit Windows 7 does not define is "bad flags":
     * ERROR_INVALID_PARAMETER.  That covers MEM_DECOMMIT, MEM_RELEASE and MEM_FREE passed
     * here by mistake, and MEM_RESET_UNDO, which only Windows 8 and later accept. */
    const DWORD defined = MEM_COMMIT | MEM_RESERVE | MEM_RESET | MEM_TOP_DOWN | MEM_WRITE_WATCH |
                          MEM_LARGE_PAGES | VM_MEM_PHYSICAL;
    if (alloc_type & ~defined)
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);
    /* Rule: at least one of MEM_COMMIT, MEM_RESERVE and MEM_RESET is required (MEM_TOP_DOWN
     * and the other modifiers alone are not a request), and of MEM_RESET: "This value
     * cannot be used with any other value."  ERROR_INVALID_PARAMETER. */
    if (!(alloc_type & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)) ||
        ((alloc_type & MEM_RESET) && alloc_type != MEM_RESET))
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);
    /* Rule: "If you specify [MEM_LARGE_PAGES], you must also specify MEM_RESERVE and
     * MEM_COMMIT"; "If you specify [MEM_WRITE_WATCH], you must also specify MEM_RESERVE";
     * MEM_PHYSICAL "must be used with MEM_RESERVE and no other values" ("The MEM_COMMIT
     * value must not be set.  The page protection must be set to PAGE_READWRITE.")
     * kernel: MEM_LARGE_PAGES without MEM_COMMIT, or together with MEM_PHYSICAL or
     * MEM_WRITE_WATCH; either of those two without MEM_RESERVE; and MEM_PHYSICAL with
     * MEM_COMMIT are refused with the other flag errors, before the protection and the
     * size are looked at: ERROR_INVALID_PARAMETER.
     * UNSURE, MEM_LARGE_PAGES: Windows versions differ.  Wine's tests accept
     * ERROR_PRIVILEGE_NOT_HELD instead of ERROR_INVALID_PARAMETER from some version for
     * MEM_RESERVE | MEM_LARGE_PAGES, and success from some version for
     * MEM_RESERVE | MEM_PHYSICAL | MEM_LARGE_PAGES; which one Windows 7 is was not found
     * out.  MEM_LARGE_PAGES | MEM_COMMIT without MEM_RESERVE passes this check, as it
     * does in the kernel although the documentation asks for both. */
    if ((alloc_type & MEM_LARGE_PAGES) &&
        (!(alloc_type & MEM_COMMIT) || (alloc_type & (VM_MEM_PHYSICAL | MEM_WRITE_WATCH))))
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);
    if ((alloc_type & (MEM_WRITE_WATCH | VM_MEM_PHYSICAL)) && !(alloc_type & MEM_RESERVE))
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);
    if ((alloc_type & VM_MEM_PHYSICAL) && (alloc_type & MEM_COMMIT))
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);
    /* Rule: the protection must be valid in every call, also for a plain MEM_RESERVE and
     * for MEM_RESET, which otherwise ignores it; and it must be PAGE_READWRITE with
     * MEM_PHYSICAL.  ERROR_INVALID_PARAMETER. */
    if (!protect_valid(protect) || ((alloc_type & VM_MEM_PHYSICAL) && protect != PAGE_READWRITE))
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);
    /* Rule: size 0 fails with ERROR_INVALID_PARAMETER; so does a range that starts at or
     * reaches past the highest user address (which also catches addr + size wrapping). */
    if (size == 0 || addr >= USER_END || size > USER_END - addr)
        return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);

    /* Valid on Windows 7, not handled here (see vm_unhandled). */
    if (alloc_type & MEM_WRITE_WATCH)
        VM_UNIMPLEMENTED("rt_vm_alloc: MEM_WRITE_WATCH (allocation type %#x, address %#llx, size %#llx): "
                         "writes are not tracked", alloc_type, (unsigned long long)addr, (unsigned long long)size);
    if (alloc_type & VM_MEM_PHYSICAL)
        VM_UNIMPLEMENTED("rt_vm_alloc: MEM_PHYSICAL (allocation type %#x, address %#llx, size %#llx): "
                         "address windowing is not built", alloc_type, (unsigned long long)addr,
                         (unsigned long long)size);

    /* Section 5.2: MEM_LARGE_PAGES fails with ERROR_PRIVILEGE_NOT_HELD, as it does on
     * Windows for a process without the lock-pages privilege.  kernel: the privilege is
     * checked after all the argument checks above, so a call that is invalid anyway
     * reports ERROR_INVALID_PARAMETER.
     * UNSURE: "The size and alignment must be a multiple of the large-page minimum."
     * Wine's tests show Windows answering a size that is not such a multiple with
     * ERROR_INVALID_PARAMETER on some versions and with ERROR_PRIVILEGE_NOT_HELD on others.
     * The multiple is not checked here: every such call gets ERROR_PRIVILEGE_NOT_HELD. */
    if (alloc_type & MEM_LARGE_PAGES)
        return alloc_fail(ERROR_PRIVILEGE_NOT_HELD, addr, size, alloc_type, protect);

    /* MEM_TOP_DOWN is a placement hint; it is ignored (rt/vm.h). */

    const bool commit = (alloc_type & MEM_COMMIT) != 0;
    DWORD      err = 0;
    Unhandled  unhandled = UNHANDLED_NONE;
    RtVmType   found_type = type;
    uint64_t   result = 0, log_base = 0, log_end = 0;
    Inexact    ix = { 0, 0 };

    /* ---- MEM_RESET: succeeds and does nothing (section 5.2) -------------------------------- */
    if (alloc_type == MEM_RESET) {
        /* UNSURE: MEM_RESET with no address.  The documentation does not cover it; the
         * kernel, as ReactOS reimplements it, would go on to RESERVE a new region, which
         * no program can want.  Not handled. */
        if (addr == 0)
            VM_UNIMPLEMENTED("rt_vm_alloc: MEM_RESET with address 0 (size %#llx): what Windows does is not known",
                             (unsigned long long)size);
        /* kernel (and Wine's tests on Windows): the range is rounded outward to pages and
         * must lie inside one reservation, else ERROR_INVALID_ADDRESS; the state of the
         * pages does not matter (resetting reserved pages succeeds).  The contents are
         * kept: "does not guarantee that the range ... will contain zeros" lets the system
         * keep them, and keeping them is the one answer that is always allowed. */
        os_unfair_lock_lock(&vm_lock);
        Region *r = find_region(addr);
        if (!r) {
            err = ERROR_INVALID_ADDRESS;
        } else if (up(addr + size, GPAGE) > region_end(r)) {
            err = ERROR_INVALID_ADDRESS;
        } else if (r->type != RT_VM_PRIVATE) {
            unhandled = UNHANDLED_RESET_TYPE;
            found_type = r->type;
        } else {
            result = down(addr, GPAGE);
        }
        os_unfair_lock_unlock(&vm_lock);
        if (err == ERROR_INVALID_ADDRESS && is_host_memory(addr)) unhandled = UNHANDLED_HOST_MEMORY;
        if (unhandled) vm_unhandled(unhandled, "rt_vm_alloc(MEM_RESET)", addr, size, alloc_type, type, found_type);
        if (err) return alloc_fail(err, addr, size, alloc_type, protect);
        return result;
    }

    /* ---- a new region ------------------------------------------------------------------------ */
    /* Rule: MEM_RESERVE (with or without MEM_COMMIT) makes a new reservation.  So does
     * MEM_COMMIT alone when the address is NULL: "reserve and commit". */
    if ((alloc_type & MEM_RESERVE) || addr == 0) {
        /* Rule: PAGE_WRITECOPY and PAGE_EXECUTE_WRITECOPY are "not supported by the
         * VirtualAlloc or VirtualAllocEx functions"; kernel: for private memory the error
         * is ERROR_INVALID_PARAMETER.  They are for an image or a view, which only the
         * loader and the file unit create. */
        if (type == RT_VM_PRIVATE && (protect & PAGE_ANY_WRITECOPY))
            return alloc_fail(ERROR_INVALID_PARAMETER, addr, size, alloc_type, protect);

        /* Rule: with an address, the base is the address rounded DOWN to 64 KB and the
         * region runs to the end of the last page that holds a byte of [addr, addr + size).
         * With no address, the size is rounded up to a page. */
        uint64_t base = addr ? down(addr, GRAN) : 0;
        uint64_t bytes = addr ? up(addr + size, GPAGE) - base : up(size, GPAGE);
        uint64_t npages = bytes / GPAGE;
        uint16_t *pg = NULL;
        /* The side array, padded to whole host pages.  calloc: zero means reserved, and
         * the allocator hands a large array out as untouched zero pages, so a huge
         * reservation costs nothing until pages are committed. */
        if (bytes <= RT_VM_ARENA_SIZE) pg = calloc(up(npages, host_page / GPAGE), sizeof *pg);
        /* Reserve and commit in one step commits the whole region.  (Filled in before the
         * lock is taken: nobody else can see the array yet.) */
        if (pg && commit)
            for (uint64_t g = 0; g < npages; g++) pg[g] = (uint16_t)protect;

        os_unfair_lock_lock(&vm_lock);
        if (addr) {
            /* Rule: a requested range that is not entirely free fails with
             * ERROR_INVALID_ADDRESS.  Here the guest's address space is the two arenas
             * (section 5.2): everything outside them counts as taken. */
            uint64_t end = base + bytes;
            bool in_image = rt_vm_in_image_arena(base) && end <= RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE;
            bool in_vm = rt_vm_in_vm_arena(base) && end <= RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE;
            if (!(in_image || in_vm) || !range_free(base, end)) err = ERROR_INVALID_ADDRESS;
        } else {
            /* Rule: no room for the reservation is ERROR_NOT_ENOUGH_MEMORY.  First fit:
             * the lowest 64 KB aligned gap of the VM arena that is big enough. */
            base = bytes <= RT_VM_ARENA_SIZE ? first_fit(bytes) : 0;
            if (!base) err = ERROR_NOT_ENOUGH_MEMORY;
        }
        if (!err && !pg) err = ERROR_NOT_ENOUGH_MEMORY;
        if (!err) {
            Region nr = { .base = base, .npages = npages, .hi = commit ? npages : 0, .pg = pg,
                          .alloc_protect = protect, .type = type };
            if (!region_insert(&nr)) {
                err = ERROR_NOT_ENOUGH_MEMORY;
            } else {
                /* The range was free, so its host pages are fresh (invariant 2): a committed
                 * region needs its access and nothing zeroed. */
                int access = commit ? host_access((uint16_t)protect) : PROT_NONE;
                if (access != PROT_NONE) host_protect(base, up(bytes, host_page), access);
                result = base;
                log_base = base;
                log_end = base + bytes;
            }
        }
        os_unfair_lock_unlock(&vm_lock);
        if (err) {
            free(pg);
            return alloc_fail(err, addr, size, alloc_type, protect);
        }
        if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
            rt_log(RT_CH_VM, RT_LL_DEBUG, "%s %#llx-%#llx %s, protect %#x",
                   commit ? "reserve and commit" : "reserve", (unsigned long long)log_base,
                   (unsigned long long)log_end, type_name(type), protect);
        return result;
    }

    /* ---- MEM_COMMIT inside an existing region ------------------------------------------------ */
    /* Rule: "the allocated pages include all pages containing one or more bytes in the
     * range from lpAddress to lpAddress+dwSize": round outward to 4 KB. */
    uint64_t first = down(addr, GPAGE), last = up(addr + size, GPAGE);

    os_unfair_lock_lock(&vm_lock);
    Region *r = find_region(addr);
    if (!r) {
        /* Rule: MEM_COMMIT without MEM_RESERVE at a non-null address "fails unless the
         * entire range has already been reserved.  The resulting error code is
         * ERROR_INVALID_ADDRESS."  (Unless it is host memory: see below the lock.) */
        err = ERROR_INVALID_ADDRESS;
    } else if (last > region_end(r)) {
        /* The same rule: the range leaves the reservation (into free space or into the
         * next one; a commit never spans two reservations). */
        err = ERROR_INVALID_ADDRESS;
    } else if (r->type != type) {
        unhandled = UNHANDLED_COMMIT_TYPE;
        found_type = r->type;
    } else if (r->type == RT_VM_PRIVATE && (protect & PAGE_ANY_WRITECOPY)) {
        err = ERROR_INVALID_PARAMETER;          /* copy-on-write on private memory, as above */
    } else {
        /* Rule: "An attempt to commit a page that is already committed does not cause the
         * function to fail."  Such a page keeps its contents.  kernel: it does get the
         * protection of this call, like every other page of the range (ReactOS does this;
         * the documentation is silent; not observed on Windows). */
        uint64_t p0 = (first - r->base) / GPAGE, p1 = (last - r->base) / GPAGE;
        apply(r, p0, p1, OP_COMMIT, (uint16_t)protect, &ix);
        if (p1 > r->hi) r->hi = p1;
        /* Rule: the result is the base of the committed pages: the address rounded down
         * to a page. */
        result = first;
    }
    os_unfair_lock_unlock(&vm_lock);

    if (err == ERROR_INVALID_ADDRESS && is_host_memory(addr)) unhandled = UNHANDLED_HOST_MEMORY;
    if (unhandled) vm_unhandled(unhandled, "rt_vm_alloc", addr, size, alloc_type, type, found_type);
    if (err) return alloc_fail(err, addr, size, alloc_type, protect);
    report_inexact(&ix);
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "commit %#llx-%#llx protect %#x", (unsigned long long)first,
               (unsigned long long)last, protect);
    return result;
}

/* ---- VirtualFree ------------------------------------------------------------------------------- */

/* Lock held.  Take region r out of the map: every host page of it is dropped (a fresh
 * inaccessible zero-fill mapping, which restores invariant (2) for the free range and
 * replaces whatever the owner mapped over it).  Returns the side array, for the caller to
 * free after the lock is released. */
static uint16_t *region_remove(Region *r) {
    uint16_t *pg = r->pg;
    host_drop(r->base, up(r->npages * GPAGE, host_page));
    size_t i = (size_t)(r - regions);
    memmove(&regions[i], &regions[i + 1], (nregions - i - 1) * sizeof *regions);
    nregions--;
    return pg;
}

static BOOL free_fail(DWORD err, uint64_t addr, uint64_t size, DWORD free_type) {
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "free(%#llx, %#llx, type %#x) failed: error %u",
               (unsigned long long)addr, (unsigned long long)size, free_type, err);
    rt_thread_set_last_error(err);
    return FALSE;
}

BOOL rt_vm_free(uint64_t addr, uint64_t size, DWORD free_type) {
    if (!host_page) rt_fatal("rt_vm_free before rt_vm_init");

    /* Rule: "If the dwFreeType parameter is MEM_RELEASE, [dwSize] must be 0".  kernel32
     * checks this first, whatever else is in the call: ERROR_INVALID_PARAMETER. */
    if (size != 0 && (free_type & MEM_RELEASE)) return free_fail(ERROR_INVALID_PARAMETER, addr, size, free_type);
    /* Rule: the type is exactly MEM_DECOMMIT or exactly MEM_RELEASE; not both, not
     * neither, no other bit.  ERROR_INVALID_PARAMETER. */
    if (free_type != MEM_RELEASE && free_type != MEM_DECOMMIT)
        return free_fail(ERROR_INVALID_PARAMETER, addr, size, free_type);
    /* Rule: a range that starts at or reaches past the highest user address is
     * ERROR_INVALID_PARAMETER (also catches addr + size wrapping). */
    if (addr >= USER_END || size > USER_END - addr) return free_fail(ERROR_INVALID_PARAMETER, addr, size, free_type);

    /* Rule: a decommit covers "all memory pages that contain one or more bytes in the
     * range".  kernel: the address is rounded down to its page before anything is compared.
     * The documentation says a release (and a decommit with size 0) needs "the base address
     * returned by the VirtualAlloc function"; the kernel compares page numbers, so an
     * address inside the first page of the region is accepted (so ReactOS has it).  That
     * is what is done here; it has not been observed on Windows. */
    uint64_t first = down(addr, GPAGE), last = up(addr + size, GPAGE);

    DWORD     err = 0;
    bool      no_region = false;
    uint16_t *dead = NULL;
    uint64_t  log_base = 0, log_end = 0;
    Inexact   ix = { 0, 0 };

    os_unfair_lock_lock(&vm_lock);
    Region *r = find_region(first);
    if (!r) {
        /* kernel: an address in no reservation (never allocated, or released already) is
         * ERROR_INVALID_ADDRESS.  (Unless it is host memory: see below the lock.) */
        no_region = true;
        err = ERROR_INVALID_ADDRESS;
    } else if (size != 0 && last > region_end(r)) {
        /* kernel: the pages of one call lie in one reservation; a decommit that runs past
         * its end, into free space or into the next reservation, fails and changes
         * nothing.  The status is STATUS_UNABLE_TO_FREE_VM, which is
         * ERROR_INVALID_PARAMETER (not ERROR_INVALID_ADDRESS, which is for a range that
         * STARTS outside any reservation). */
        err = ERROR_INVALID_PARAMETER;
    } else if (r->type != RT_VM_PRIVATE) {
        /* kernel: VirtualFree works on private memory only; on a file view or a module
         * image it fails with STATUS_UNABLE_TO_DELETE_SECTION, ERROR_INVALID_PARAMETER.
         * For MEM_DECOMMIT and for MEM_RELEASE: the loader and the file unit give their
         * regions back with rt_vm_release, so a guest VirtualFree that reaches a module or
         * a view fails here and removes nothing. */
        err = ERROR_INVALID_PARAMETER;
    } else if (size == 0 && first != r->base) {
        /* Rule: with size 0 (always, for a release) the address must be the base of the
         * reservation: ERROR_INVALID_ADDRESS. */
        err = ERROR_INVALID_ADDRESS;
    } else if (free_type == MEM_RELEASE) {
        /* Rule: release frees the whole reservation whatever the state of its pages
         * ("the function does not fail if you attempt to release pages that are in
         * different states, some reserved and some committed").  Every host page of the
         * region is dropped, which restores invariant (2) for the free range. */
        log_base = r->base;
        log_end = region_end(r);
        dead = region_remove(r);
    } else {
        /* Rule: decommit leaves the pages reserved; "the function does not fail if you
         * attempt to decommit an uncommitted page"; size 0 with the region base means the
         * whole region. */
        uint64_t p0 = (first - r->base) / GPAGE;
        uint64_t p1 = size ? (last - r->base) / GPAGE : r->npages;
        log_base = r->base + p0 * GPAGE;
        log_end = r->base + p1 * GPAGE;
        if (p1 > r->hi) p1 = r->hi;             /* nothing above the high-water mark is committed */
        if (p0 < p1) apply(r, p0, p1, OP_DECOMMIT, 0, &ix);
    }
    os_unfair_lock_unlock(&vm_lock);

    if (no_region && is_host_memory(first))
        vm_unhandled(UNHANDLED_HOST_MEMORY, "rt_vm_free", addr, size, free_type, RT_VM_PRIVATE, RT_VM_PRIVATE);
    if (err) return free_fail(err, addr, size, free_type);
    free(dead);
    report_inexact(&ix);
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "%s %#llx-%#llx", free_type == MEM_RELEASE ? "release" : "decommit",
               (unsigned long long)log_base, (unsigned long long)log_end);
    return TRUE;
}

/* ---- release by the owner of a region (rt/vm.h) ------------------------------------------------ */

BOOL rt_vm_release(uint64_t base, RtVmType type) {
    if (!host_page) rt_fatal("rt_vm_release before rt_vm_init");
    if (type != RT_VM_PRIVATE && type != RT_VM_MAPPED && type != RT_VM_IMAGE)
        rt_fatal("rt_vm_release: %d is not an RtVmType", (int)type);

    DWORD     err = 0;
    uint16_t *dead = NULL;
    uint64_t  end = 0;
    RtVmType  found = type;

    os_unfair_lock_lock(&vm_lock);
    Region *r = find_region(base);
    if (!r || r->base != base) {
        err = ERROR_INVALID_ADDRESS;            /* exactly a base: nothing is rounded for a core caller */
    } else if (r->type != type) {
        err = ERROR_INVALID_PARAMETER;          /* someone else's region */
        found = r->type;
    } else {
        end = region_end(r);
        dead = region_remove(r);
    }
    os_unfair_lock_unlock(&vm_lock);

    if (err) {
        if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
            rt_log(RT_CH_VM, RT_LL_DEBUG, "release(%#llx, %s) failed: error %u%s%s", (unsigned long long)base,
                   type_name(type), err, err == ERROR_INVALID_PARAMETER ? ": the region is " : "",
                   err == ERROR_INVALID_PARAMETER ? type_name(found) : "");
        rt_thread_set_last_error(err);
        return FALSE;
    }
    free(dead);
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "release %#llx-%#llx %s", (unsigned long long)base, (unsigned long long)end,
               type_name(type));
    return TRUE;
}

/* ---- VirtualProtect ------------------------------------------------------------------------------ */

static BOOL protect_fail(DWORD err, uint64_t addr, uint64_t size, DWORD new_protect) {
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "protect(%#llx, %#llx, %#x) failed: error %u",
               (unsigned long long)addr, (unsigned long long)size, new_protect, err);
    rt_thread_set_last_error(err);
    return FALSE;
}

BOOL rt_vm_protect(uint64_t addr, uint64_t size, DWORD new_protect, DWORD *old_protect) {
    if (!host_page) rt_fatal("rt_vm_protect before rt_vm_init");

    /* kernel: an invalid protection value is ERROR_INVALID_PARAMETER, checked before
     * anything else. */
    if (!protect_valid(new_protect)) return protect_fail(ERROR_INVALID_PARAMETER, addr, size, new_protect);
    /* Rule: "If [lpflOldProtect] is NULL or does not point to a valid variable, the
     * function fails": the kernel's probe of the pointer faults, which is ERROR_NOACCESS.
     * rt/vm.h asks callers not to pass NULL; a caller that does gets what Windows gives. */
    if (!old_protect) return protect_fail(ERROR_NOACCESS, addr, size, new_protect);
    /* Rule: size 0, and a range that starts at or reaches past the highest user address,
     * are ERROR_INVALID_PARAMETER. */
    if (size == 0 || addr >= USER_END || size > USER_END - addr)
        return protect_fail(ERROR_INVALID_PARAMETER, addr, size, new_protect);

    /* Rule: "The region of affected pages includes all pages containing one or more bytes
     * in the range from the lpAddress parameter to (lpAddress+dwSize)." */
    uint64_t first = down(addr, GPAGE), last = up(addr + size, GPAGE);

    DWORD   err = 0, old = 0;
    bool    no_region = false;
    Inexact ix = { 0, 0 };

    os_unfair_lock_lock(&vm_lock);
    Region *r = find_region(first);
    if (!r) {
        /* kernel: an address in no reservation is ERROR_INVALID_ADDRESS.  (Unless it is
         * host memory: see below the lock.) */
        no_region = true;
        err = ERROR_INVALID_ADDRESS;
    } else if (last > region_end(r)) {
        /* Rule: "The pages cannot span adjacent reserved regions that were allocated by
         * separate calls to VirtualAlloc": ERROR_INVALID_ADDRESS, also when the range runs
         * into free space. */
        err = ERROR_INVALID_ADDRESS;
    } else if (r->type == RT_VM_PRIVATE && (new_protect & PAGE_ANY_WRITECOPY)) {
        /* kernel: copy-on-write protections are not valid for private memory:
         * ERROR_INVALID_PARAMETER, checked before the pages' state is.  (For an image or
         * a view they are accepted and recorded.) */
        err = ERROR_INVALID_PARAMETER;
    } else if (r->type != RT_VM_PRIVATE && (new_protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE))) {
        /* Rule: PAGE_NOCACHE and PAGE_WRITECOMBINE "can be used only when allocating
         * private memory".  kernel: on the pages of an image or a file view VirtualProtect
         * refuses them with ERROR_INVALID_PARAMETER (from ReactOS; not observed on Windows).
         * UNSURE whether Windows also refuses them in VirtualProtect on PRIVATE memory;
         * here they are accepted there, as in VirtualAlloc.
         * NOT CHECKED: "For mapped views, this value must be compatible with the access
         * protection specified when the view was mapped."  rt/vm.h does not carry that
         * access, so the unit that creates views has to check it. */
        err = ERROR_INVALID_PARAMETER;
    } else {
        uint64_t p0 = (first - r->base) / GPAGE, p1 = (last - r->base) / GPAGE;
        /* Rule: "You can set the access protection value on committed pages only.  If the
         * state of any page in the specified region is not committed, the function fails
         * and returns without modifying the access protection of any pages":
         * ERROR_INVALID_ADDRESS. */
        bool committed = p1 <= r->hi;
        for (uint64_t g = p0; committed && g < p1; g++) committed = r->pg[g] != 0;
        if (!committed) {
            err = ERROR_INVALID_ADDRESS;
        } else {
            /* Rule: the old protection is that of the FIRST page of the range, exactly as
             * it was set, modifiers included. */
            old = r->pg[p0];
            apply(r, p0, p1, OP_PROTECT, (uint16_t)new_protect, &ix);
        }
    }
    os_unfair_lock_unlock(&vm_lock);

    if (no_region && is_host_memory(first))
        vm_unhandled(UNHANDLED_HOST_MEMORY, "rt_vm_protect", addr, size, new_protect, RT_VM_PRIVATE, RT_VM_PRIVATE);
    /* On failure *old_protect is left as it was (Windows does not document what it holds). */
    if (err) return protect_fail(err, addr, size, new_protect);
    *old_protect = old;
    report_inexact(&ix);
    if (RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG))
        rt_log(RT_CH_VM, RT_LL_DEBUG, "protect %#llx-%#llx %#x (was %#x)", (unsigned long long)first,
               (unsigned long long)last, new_protect, old);
    return TRUE;
}

/* ---- VirtualQuery -------------------------------------------------------------------------------- */

/* Rule: "For free pages, the information in the AllocationBase, AllocationProtect,
 * Protect, and Type members is undefined."  kernel: they are 0, 0, PAGE_NOACCESS and 0. */
static void describe_free(MEMORY_BASIC_INFORMATION *out, uint64_t page, uint64_t end) {
    out->BaseAddress = (PVOID)(uintptr_t)page;
    out->AllocationBase = NULL;
    out->AllocationProtect = 0;
    out->RegionSize = end - page;
    out->State = MEM_FREE;
    out->Protect = PAGE_NOACCESS;
    out->Type = 0;
}

/* An address outside both arenas (section 5.2): ask the kernel, and report a host mapping
 * as committed private memory with the protection the host gives it.  The answer is
 * limited to the stretch between the arenas that `page` lies in. */
static void describe_host(MEMORY_BASIC_INFORMATION *out, uint64_t page) {
    uint64_t lo, hi;                            /* the stretch of host address space around page */
    if (page < GRAN) {
        /* Windows never maps the first 64 KB and reports it as free.  So does this. */
        describe_free(out, page, GRAN);
        return;
    }
    if (page < RT_IMAGE_ARENA_BASE)     { lo = GRAN; hi = RT_IMAGE_ARENA_BASE; }
    else if (page < RT_VM_ARENA_BASE)   { lo = RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE; hi = RT_VM_ARENA_BASE; }
    else                                { lo = RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE; hi = USER_END; }

    uint64_t start, end;
    int prot;
    if (!host_region(page, &start, &end, &prot) || start >= hi) { describe_free(out, page, hi); return; }
    if (start > page) { describe_free(out, page, start); return; }
    if (start < lo) start = lo;
    if (end > hi) end = hi;

    DWORD p;
    switch (prot & (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)) {
    case VM_PROT_NONE:                                      p = PAGE_NOACCESS; break;
    case VM_PROT_READ:                                      p = PAGE_READONLY; break;
    case VM_PROT_WRITE:
    case VM_PROT_READ | VM_PROT_WRITE:                      p = PAGE_READWRITE; break;
    case VM_PROT_EXECUTE:                                   p = PAGE_EXECUTE; break;
    case VM_PROT_READ | VM_PROT_EXECUTE:                    p = PAGE_EXECUTE_READ; break;
    default:                                                p = PAGE_EXECUTE_READWRITE; break;
    }
    out->BaseAddress = (PVOID)(uintptr_t)page;
    out->AllocationBase = (PVOID)(uintptr_t)start;
    out->AllocationProtect = p;
    out->RegionSize = end - page;
    out->State = MEM_COMMIT;
    out->Protect = p;
    out->Type = MEM_PRIVATE;
}

SIZE_T rt_vm_query(uint64_t addr, MEMORY_BASIC_INFORMATION *out) {
    if (!host_page) rt_fatal("rt_vm_query before rt_vm_init");

    /* Rule: "If lpAddress specifies an address above the highest memory address accessible
     * to the process, the function fails with ERROR_INVALID_PARAMETER", and the result is 0. */
    if (addr >= USER_END) { rt_thread_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    /* kernel: a buffer it cannot write is ERROR_NOACCESS.  (A buffer that is too small,
     * ERROR_BAD_LENGTH, is the shim's to check: this interface has no length.) */
    if (!out) { rt_thread_set_last_error(ERROR_NOACCESS); return 0; }

    /* Rule: the address "is rounded down to the next page boundary": that is BaseAddress.
     * RegionSize counts FORWARD from there over the pages with the same attributes ("if
     * there is a 40 megabyte (MB) region of free memory, and VirtualQuery is called on a
     * page that is 10 MB into the region, the function will obtain a state of MEM_FREE and
     * a size of 30 MB").  Pages below the queried one are not part of the answer. */
    uint64_t page = down(addr, GPAGE);
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof mbi);                /* also the two reserved fields */

    if (!in_arena(page)) {
        describe_host(&mbi, page);
        *out = mbi;
        return sizeof *out;
    }

    const uint64_t arena_end = rt_vm_in_image_arena(page) ? RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE
                                                           : RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE;
    os_unfair_lock_lock(&vm_lock);
    Region *r = find_region(page);
    if (!r) {
        /* Free space runs to the next region of this arena or to the arena's end. */
        size_t i = first_region_above(page);
        uint64_t end = i < nregions && regions[i].base < arena_end ? regions[i].base : arena_end;
        describe_free(&mbi, page, end);
    } else {
        /* Rule: the pages of one answer have the same state and the same access, and "are
         * part of the same initial allocation of pages created by a single call to
         * VirtualAlloc, MapViewOfFile": the run ends where the state or the protection
         * changes, and at the end of the reservation, even when the next reservation is
         * adjacent and alike.  AllocationProtect is "the memory protection option when the
         * region was initially allocated".  "For reserved pages, the information in the
         * Protect member is undefined"; kernel: it is 0. */
        uint64_t p = (page - r->base) / GPAGE, q = p + 1;
        uint16_t rec = p < r->hi ? r->pg[p] : 0;
        while (q < r->hi && r->pg[q] == rec) q++;
        if (rec == 0 && q >= r->hi) q = r->npages;          /* everything above the mark is reserved */
        mbi.BaseAddress = (PVOID)(uintptr_t)page;
        mbi.AllocationBase = (PVOID)(uintptr_t)r->base;
        mbi.AllocationProtect = r->alloc_protect;
        mbi.RegionSize = (q - p) * GPAGE;
        mbi.State = rec ? MEM_COMMIT : MEM_RESERVE;
        mbi.Protect = rec;
        mbi.Type = r->type == RT_VM_IMAGE ? MEM_IMAGE : r->type == RT_VM_MAPPED ? MEM_MAPPED : MEM_PRIVATE;
    }
    os_unfair_lock_unlock(&vm_lock);
    *out = mbi;
    return sizeof *out;
}

/* ---- rt_vm_probe: the query that never waits (rt/vm.h; review finding R14) ------------------- */

RtVmProbeState rt_vm_probe(uint64_t addr, RtVmProbe *out) {
    RtVmProbe p;
    memset(&p, 0, sizeof p);
    if (!in_arena(addr)) {
        p.state = RT_VM_PROBE_OUTSIDE;
    } else if (!os_unfair_lock_trylock(&vm_lock)) {
        p.state = RT_VM_PROBE_BUSY;
    } else {
        const Region *r = find_region(addr);
        if (!r) {
            p.state = RT_VM_PROBE_FREE;
        } else {
            uint64_t i = (addr - r->base) / GPAGE;
            uint16_t rec = i < r->hi ? r->pg[i] : 0;
            p.state = rec ? RT_VM_PROBE_COMMITTED : RT_VM_PROBE_RESERVED;
            p.base = r->base;
            p.size = r->npages * GPAGE;
            p.protect = rec;
            p.type = r->type;
        }
        os_unfair_lock_unlock(&vm_lock);
    }
    if (out) *out = p;
    return p.state;
}

/* For the test of rt_vm_probe only (declared in core/report.h): take or release the map's
 * lock on the calling thread, so that the BUSY answer can be asked for on purpose. */
void rt_vm_test_hold_lock(bool hold) {
    if (hold) os_unfair_lock_lock(&vm_lock);
    else os_unfair_lock_unlock(&vm_lock);
}
