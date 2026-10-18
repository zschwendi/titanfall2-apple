/* Heaps (docs/RUNTIME.md section 5.4).  Interface and specification: rt/heap.h.
 *
 * DESIGN.  The process heap is a dedicated host malloc zone (not the default zone) and each
 * HeapCreate heap is another zone, destroyed whole by HeapDestroy.  Every block is a zone
 * block of 16 + requested bytes: the first 16 are an RtHeapBlock { magic, heap id, requested
 * size } and the guest gets the address after it, which is 16-byte aligned because the zone
 * returns 16-byte aligned memory.  HeapSize answers from the header, so it is exact.
 *
 * A heap handle is the address of an RtHeap in a static table (heap_slab), not a handle-table
 * entry.  Validating a handle is therefore pointer arithmetic plus a magic check: no host
 * call, and a wild value cannot make the check itself fault.  A destroyed heap's slot keeps
 * its address (magic RT_HEAP_MAGIC_DEAD) and is reused only after every other slot has been
 * used, so use of a stale handle is caught for a long time rather than aliasing a new heap.
 *
 * A pointer given to HeapFree, HeapReAlloc or HeapSize is validated without reading it
 * first: it must be 16-byte aligned and inside the guest address range, and the heap's own
 * zone must say it is the start of one of its blocks (zone->size, which is safe for any
 * address).  Only then are the header's magic and heap id read.  So a stack address, a
 * malloc pointer, an interior pointer, a block of another heap or of a destroyed heap all
 * end in a report that names the pointer, instead of a crash in the host allocator.
 *
 * WHERE WINDOWS DECIDES THE BEHAVIOUR, and how sure this file is of the rule.  "Docs" means
 * the Microsoft documentation of the function; "memory" means the author remembers what
 * ntdll/kernel32 do and has not seen it documented; "unsure" is said where it applies.
 *
 *  HeapAlloc failure      NULL, and last error is ERROR_NOT_ENOUGH_MEMORY (Wine
 *                         kernel32/tests/heap.c test_HeapCreate, fixed-size heaps, on Windows 7
 *                         too; the Microsoft page says HeapAlloc does not call SetLastError, and
 *                         the test contradicts the page).  HeapReAlloc still leaves it alone:
 *                         its check for that code is marked todo_wine in the same test.
 *  HEAP_GENERATE_EXCEPTIONS  Docs: failure raises STATUS_NO_MEMORY instead of returning NULL;
 *                         the flag given to HeapCreate applies to every call on the heap.
 *                         Exceptions are plan A (RUNTIME.md 9.3): here that is a fatal report.
 *  HEAP_NO_SERIALIZE      Docs: the caller promises there is no concurrent access, so the
 *                         heap may skip its lock.  Every zone is thread-safe, so the promise
 *                         is never needed; accepting the flag changes nothing observable.
 *  HEAP_ZERO_MEMORY       Docs: HeapAlloc zeroes the block; HeapReAlloc zeroes only the
 *                         added bytes.  Exactly the requested bytes: not the slack the zone
 *                         rounds up to.
 *  HEAP_REALLOC_IN_PLACE_ONLY  Docs: HeapReAlloc fails (returning NULL, original block
 *                         unchanged) if the block cannot be resized without moving.  A block
 *                         can stay put when it shrinks, or grows within the slack the zone
 *                         rounded it up to.  Windows can also grow a block into a free
 *                         neighbour, which a malloc zone cannot be asked to do; this file then
 *                         fails where Windows might have succeeded.  The docs allow that
 *                         failure and callers must have a fallback.
 *  HEAP_CREATE_ENABLE_EXECUTE  Accepted and logged: guest bytes are never executed (RUNTIME.md
 *                         5.2), so a call into such a heap is a dispatch miss that says so.
 *  Other flag bits        Windows masks flags it does not know about (memory; UNSURE for
 *                         which function masks what).  Here a bit a function does not take,
 *                         for example HEAP_ZERO_MEMORY on HeapFree, is fatal and names it.
 *  Size 0                 HeapAlloc(0) is a valid, unique, freeable pointer with HeapSize 0
 *                         (docs).  HeapReAlloc to 0 is the same: the block stays live with
 *                         size 0 (memory; UNSURE), it is not freed.
 *  HeapFree(NULL)         TRUE (the docs call it undefined; ntdll returns TRUE: memory).  With a
 *                         NULL heap handle too: the handle is not checked then (Wine
 *                         kernel32/tests/heap.c test_HeapCreate: HeapFree(NULL, 0, NULL) is
 *                         TRUE).  With any other handle the handle is checked first, so a bad
 *                         handle is fatal even with a NULL block (no source seen for that case:
 *                         UNSURE what Windows does).
 *  HeapReAlloc(NULL)      Fatal.  UNSURE what Windows does (ntdll fails with NULL, from
 *                         memory); no sound caller passes NULL, so stopping is the safe answer.
 *  HeapSize on a pointer that is not a live block of this heap: (SIZE_T)-1, no last error
 *                         (docs).  HeapFree and HeapReAlloc on one are fatal instead (a
 *                         Windows heap would be corrupted).
 *  HeapDestroy(process heap)  FALSE and ERROR_INVALID_HANDLE (memory of kernel32).
 *  HeapValidate           Docs: zero for an invalid heap or block, and no last error.  The
 *                         one call for which a bad heap handle is not fatal.  With no block
 *                         it answers for the handle only: nothing is walked (rt/heap.h).
 *  Fixed-size heaps       HeapCreate with a nonzero maximum makes a heap that cannot grow.
 *                         One block must be smaller than 0xFF000 bytes.  Docs: the HeapAlloc
 *                         page gives 0x7FFF8 (a 32-bit figure); the HeapCreate page says
 *                         "slightly less than 1,024 KB" for a 64-bit process.  Wine
 *                         kernel32/tests/heap.c test_HeapCreate, fixed-size heaps, on 64-bit:
 *                         a block of 0x80000 fits and one of 0x100000 does not; those two
 *                         points are all that is known of Windows here.  0xFF000 (1,024 KB less
 *                         one 4 KB page) is OUR figure, chosen inside that range, not Windows'.
 *                         The total is held to the maximum (rounded up to a 4 KB page)
 *                         counting each block as 16 + its size rounded up to 16, which is how
 *                         a 64-bit Windows heap sizes its entries.  UNSURE: Windows also spends
 *                         some of the maximum on its own heap structures, so it fills up a few
 *                         hundred bytes sooner than this file does, and its limit for one
 *                         block may differ from 0xFF000.
 *  Huge requests         HeapAlloc commits memory, so Windows fails any request above the commit
 *                         limit (RAM plus page file) with NULL.  This runtime does not emulate
 *                         that limit: a host block is only address space until it is touched.
 *                         It does refuse, with NULL, a request of 4 TiB or more (see too_big).
 *  HeapCreate sizes       The initial size is only a hint (docs: the memory committed up
 *                         front).  A maximum smaller than the initial size is fatal (docs:
 *                         "must be smaller"; what Windows does is UNSURE).  A size above the
 *                         guest address limit makes HeapCreate fail with
 *                         ERROR_NOT_ENOUGH_MEMORY (UNSURE which code).
 *
 * Every block must lie entirely at or below the guest address limit (rt_machine()
 * ->max_address, 0x7FFFFFEFFFF, 8 TB): the design assumes host zone addresses stay there
 * (RUNTIME.md 1.2).  Each block is checked when it is made or moved, and one above the
 * limit stops the process with a report naming the address.
 */
#include <malloc/malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "rt/heap.h"
#include "rt/machine.h"

/* ---- constants ------------------------------------------------------------------------------ */

#ifndef HEAP_CREATE_ENABLE_EXECUTE
#define HEAP_CREATE_ENABLE_EXECUTE  0x00040000u   /* HeapCreate only; winnt.h */
#endif

#define RT_HEAP_MAGIC_DEAD   0x64616544u          /* "Dead": the slot of a destroyed heap */
#define RT_HEAP_SLOTS        1024u                /* heaps alive at once; see heap_new */

/* In a heap that cannot grow, one block must be smaller than this (review item Y5, in
 * docs/reviews/win-behaviour-check-4.md).  The HeapAlloc page's 0x7FFF8 is the 32-bit figure; the
 * HeapCreate page says "slightly less than 1,024 KB" for 64-bit, and Wine kernel32/tests/heap.c
 * test_HeapCreate shows 0x80000 fits and 0x100000 does not.  0xFF000 (1,024 KB less one 4 KB page)
 * is OUR figure, chosen inside that range: the exact limit is not known from Windows. */
#define FIXED_HEAP_BLOCK_LIMIT  UINT64_C(0xFF000)

/* The flags each function takes; any other bit is fatal (check_flags). */
#define FLAGS_CREATE   (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_CREATE_ENABLE_EXECUTE)
#define FLAGS_ALLOC    (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY)
#define FLAGS_REALLOC  (FLAGS_ALLOC | HEAP_REALLOC_IN_PLACE_ONLY)
#define FLAGS_FREE     (HEAP_NO_SERIALIZE)        /* HeapFree and HeapSize */

#define BLOCK_HEADER   ((uint64_t)sizeof(RtHeapBlock))

/* ---- the heap header ------------------------------------------------------------------------- */

/* What a heap handle points to.  Fixed-width types only; the first word is the magic
 * (rt/heap.h).  One cache line each, so heaps used by different threads do not share one. */
typedef struct __attribute__((aligned(64))) RtHeap {
    uint32_t       magic;         /* RT_HEAP_MAGIC; RT_HEAP_MAGIC_DEAD after HeapDestroy; 0 never used */
    uint32_t       id;            /* the id every block of this heap carries; never reused */
    uint32_t       options;       /* HeapCreate flags */
    uint32_t       reserved_;
    uint64_t       initial_size;  /* as asked, rounded up to a 4 KB page; informational */
    uint64_t       maximum_size;  /* rounded up to a 4 KB page; 0 = the heap grows */
    malloc_zone_t *zone;
    uint64_t       in_use;        /* fixed-size heaps only: bytes charged to live blocks (atomic) */
} RtHeap;
_Static_assert(sizeof(RtHeap) == 64, "RtHeap is one cache line");
_Static_assert(offsetof(RtHeap, magic) == 0, "the magic is the first word of the heap header");

static RtHeap          heap_slab[RT_HEAP_SLOTS];
static pthread_mutex_t slab_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned        slab_fresh;                    /* slots [0, slab_fresh) have been used once */
static uint16_t        dead_queue[RT_HEAP_SLOTS];     /* destroyed slots, oldest first */
static unsigned        dead_head, dead_count;
static uint32_t        last_id;                       /* guarded by slab_lock */
static RtHeap         *process_heap;                  /* written once, by rt_heap_init */

/* ---- small helpers --------------------------------------------------------------------------- */

static inline uint64_t round_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

/* What a block of `size` requested bytes costs a fixed-size heap. */
static inline uint64_t charge(uint64_t size) { return BLOCK_HEADER + round_up(size, 16); }

static inline unsigned long long ull(uint64_t v) { return (unsigned long long)v; }

/* Fail loudly on a flag bit the function does not take (never ignore it silently). */
static void check_flags(const char *fn, DWORD flags, DWORD allowed) {
    if (flags & ~allowed)
        rt_fatal("heap: %s: flags %#x contain %#x, which this function does not take (it takes %#x). "
                 "Windows would mask or reject it; the runtime stops instead of guessing which",
                 fn, flags, flags & ~allowed, allowed);
}

/* ---- heap handles ---------------------------------------------------------------------------- */

static _Noreturn void bad_heap(const void *handle, uint32_t magic, bool in_slab, const char *fn) {
    if (!handle)
        rt_fatal("heap: %s: the heap handle is NULL (HeapCreate returns NULL when it fails)", fn);
    if (!in_slab)
        rt_fatal("heap: %s: %p is not a heap handle: it is not the address of a heap header made by "
                 "HeapCreate or GetProcessHeap", fn, handle);
    if (magic == RT_HEAP_MAGIC_DEAD)
        rt_fatal("heap: %s: heap handle %p belongs to a heap that HeapDestroy already destroyed", fn, handle);
    rt_fatal("heap: %s: heap handle %p is an unused heap header (magic %#x, not %#x)", fn, handle, magic, RT_HEAP_MAGIC);
}

/* The RtHeap a handle names, or fatal.  Pointer arithmetic and one load: cheap and safe
 * for any value. */
static inline RtHeap *heap_ref(HANDLE handle, const char *fn) {
    uint64_t off = (uint64_t)(uintptr_t)handle - (uint64_t)(uintptr_t)heap_slab;
    if (__builtin_expect(off >= sizeof heap_slab || off % sizeof(RtHeap) != 0, 0))
        bad_heap(handle, 0, false, fn);
    RtHeap *h = (RtHeap *)handle;
    uint32_t m = __atomic_load_n(&h->magic, __ATOMIC_ACQUIRE);
    if (__builtin_expect(m != RT_HEAP_MAGIC, 0)) bad_heap(handle, m, true, fn);
    return h;
}

/* ---- blocks ---------------------------------------------------------------------------------- */

typedef enum {
    B_OK = 0,
    B_NULL,           /* no pointer */
    B_MISALIGNED,     /* not 16-byte aligned: no block starts there */
    B_RANGE,          /* outside the addresses the guest can have */
    B_NOT_IN_HEAP,    /* the heap's zone does not know a block here */
    B_BAD_MAGIC,      /* a block of the zone, but its header is not a live block's (freed, or overwritten) */
    B_WRONG_ID        /* a live-looking header that names another heap */
} BlockStatus;

/* Is `p` the start of a live block of `h`?  Never faults, whatever p is. */
static BlockStatus block_check(const RtHeap *h, const void *p, RtHeapBlock **out) {
    uint64_t a = (uint64_t)(uintptr_t)p;
    if (!p) return B_NULL;
    if (a & 15) return B_MISALIGNED;
    const RtMachine *mc = rt_machine();
    if (a < mc->min_address + BLOCK_HEADER || a > mc->max_address) return B_RANGE;
    RtHeapBlock *b = (RtHeapBlock *)p - 1;
    if (h->zone->size(h->zone, b) == 0) return B_NOT_IN_HEAP;      /* safe for any address */
    if (__atomic_load_n(&b->magic, __ATOMIC_ACQUIRE) != RT_HEAP_BLOCK_MAGIC) return B_BAD_MAGIC;
    if (b->heap_id != h->id) return B_WRONG_ID;
    *out = b;
    return B_OK;
}

/* For the report only: which live heap, if any, has a block starting at `b`. */
static const RtHeap *find_owner(const void *b) {
    for (unsigned i = 0; i < RT_HEAP_SLOTS; i++) {
        const RtHeap *o = &heap_slab[i];
        if (__atomic_load_n(&o->magic, __ATOMIC_ACQUIRE) == RT_HEAP_MAGIC && o->zone->size(o->zone, b) != 0)
            return o;
    }
    return NULL;
}

static _Noreturn void bad_block(const RtHeap *h, const void *p, BlockStatus s, const char *fn) {
    const RtHeapBlock *b = p ? (const RtHeapBlock *)p - 1 : NULL;
    switch (s) {
    case B_NULL:
        rt_fatal("heap: %s: NULL block in heap %u", fn, h->id);
    case B_MISALIGNED:
        rt_fatal("heap: %s: %p is not a block of heap %u: blocks are 16-byte aligned and this address is not "
                 "(an interior pointer, or not a heap block at all)", fn, p, h->id);
    case B_RANGE:
        rt_fatal("heap: %s: %p is not a block of heap %u: it is outside the guest address range %#llx..%#llx",
                 fn, p, h->id, ull(rt_machine()->min_address), ull(rt_machine()->max_address));
    case B_NOT_IN_HEAP: {
        const RtHeap *o = find_owner(b);
        if (o)
            rt_fatal("heap: %s: %p is not a block of heap %u: it is a block of heap %u (freed to, or "
                     "reallocated in, the wrong heap)", fn, p, h->id, o->id);
        rt_fatal("heap: %s: %p is not a block of heap %u, nor of any live heap: it was never allocated by the "
                 "runtime heaps, is an interior pointer, was already freed (a double free), or belonged to a "
                 "heap that was destroyed", fn, p, h->id);
    }
    case B_BAD_MAGIC:
        rt_fatal("heap: %s: %p in heap %u has a block header with magic %#x, not %#x: the block was already "
                 "freed (a double free; a freed block's magic is 0) or its header was overwritten",
                 fn, p, h->id, b->magic, RT_HEAP_BLOCK_MAGIC);
    case B_WRONG_ID:
        rt_fatal("heap: %s: %p was passed with heap %u but its block header names heap id %u (freed to, or "
                 "reallocated in, the wrong heap, or the header was overwritten)", fn, p, h->id, b->heap_id);
    case B_OK:
        break;
    }
    rt_fatal("heap: %s: block status %d is not an error", fn, (int)s);
}

/* Every block lies entirely inside the guest address range (RUNTIME.md 1.2, 5.4): the limit
 * the design relies on is the top of it, 8 TB. */
static void check_extent(const RtHeap *h, const void *base, uint64_t total, const char *fn) {
    const RtMachine *mc = rt_machine();
    uint64_t lo = (uint64_t)(uintptr_t)base;
    if (lo < mc->min_address || lo > mc->max_address || total - 1 > mc->max_address - lo)
        rt_fatal("heap: %s: the host malloc zone of heap %u returned the block %#llx..%#llx, which is not "
                 "entirely inside the guest address range %#llx..%#llx (the limit is 8 TB). The design assumes "
                 "zone addresses stay below 8 TB (docs/RUNTIME.md 1.2, 5.4); the fallback is an allocator of "
                 "our own over a range reserved in the VM arena",
                 fn, h->id, ull(lo), ull(lo + total - 1), ull(mc->min_address), ull(mc->max_address));
    if (lo & 15)
        rt_fatal("heap: %s: the host malloc zone of heap %u returned %#llx, which is not 16-byte aligned",
                 fn, h->id, ull(lo));
}

/* ---- fixed-size heaps' bookkeeping ----------------------------------------------------------- */

/* Take `n` bytes of a fixed-size heap's maximum, or report that it does not fit.  A
 * compare-and-swap loop: concurrent callers can never take more than the maximum in total. */
static bool reserve(RtHeap *h, uint64_t n) {
    uint64_t cur = __atomic_load_n(&h->in_use, __ATOMIC_RELAXED);
    do {
        if (n > h->maximum_size || cur > h->maximum_size - n) return false;
    } while (!__atomic_compare_exchange_n(&h->in_use, &cur, cur + n, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return true;
}

static void release(RtHeap *h, uint64_t n) {
    __atomic_fetch_sub(&h->in_use, n, __ATOMIC_RELAXED);
}

/* ---- allocation ------------------------------------------------------------------------------ */

/* The way every allocation failure ends: NULL.  Only rt_heap_alloc then sets the last error
 * (ERROR_NOT_ENOUGH_MEMORY); HeapReAlloc and rt_galloc leave it alone.
 * With HEAP_GENERATE_EXCEPTIONS, in the call or in HeapCreate, Windows raises STATUS_NO_MEMORY
 * instead, and exceptions are plan A (RUNTIME.md 9.3). */
static void *alloc_failed(const RtHeap *h, DWORD flags, const char *fn, uint64_t size, const char *why) {
    if ((flags | h->options) & HEAP_GENERATE_EXCEPTIONS)
        rt_fatal("heap: %s: heap %u cannot satisfy a request for %llu bytes (%s), and HEAP_GENERATE_EXCEPTIONS "
                 "is set, so Windows raises STATUS_NO_MEMORY (0xC0000017) here. Exceptions are plan A "
                 "(docs/RUNTIME.md 9.3): the process stops", fn, h->id, ull(size), why);
    rt_log(RT_CH_VM, RT_LL_WARN, "heap: %s: heap %u cannot satisfy a request for %llu bytes (%s); returning NULL",
           fn, h->id, ull(size), why);
    return NULL;
}

/* A request refused for its size alone, without asking the host.  Two reasons: no block of
 * `size` plus its header can fit below the guest address limit; and a block of 4 TiB or more
 * is half the guest's whole 8 TB, the arenas of section 1.2 take a slice of it from 1 TB, and
 * the host (which hands out address space lazily, so it grants such a request) chooses where
 * the block goes, so it can end past the limit.  That would reach the address check in
 * check_extent, which stops the process because it means the host placed memory where the
 * design says it cannot.  An impossible request is the caller's failure, not a placement
 * problem, and Windows answers it with NULL (it cannot commit that much). */
#define MAX_BLOCK_SIZE  (UINT64_C(1) << 42)

static bool too_big(uint64_t size) {
    return size >= MAX_BLOCK_SIZE || size > rt_machine()->max_address - BLOCK_HEADER;
}

static void *block_alloc(RtHeap *h, DWORD flags, uint64_t size, const char *fn) {
    if (too_big(size))
        return alloc_failed(h, flags, fn, size, "larger than any block the guest address space can hold");
    uint64_t cost = 0;
    if (h->maximum_size) {
        if (size >= FIXED_HEAP_BLOCK_LIMIT)
            return alloc_failed(h, flags, fn, size, "a heap created with a maximum size cannot hold a block of "
                                "0xFF000 bytes or more");
        cost = charge(size);
        if (!reserve(h, cost))
            return alloc_failed(h, flags, fn, size, "the heap was created with a maximum size and is full");
    }
    uint64_t total = BLOCK_HEADER + size;
    RtHeapBlock *b = (flags & HEAP_ZERO_MEMORY) ? malloc_zone_calloc(h->zone, 1, total)
                                                : malloc_zone_malloc(h->zone, total);
    if (!b) {
        if (cost) release(h, cost);
        return alloc_failed(h, flags, fn, size, "the host allocator refused");
    }
    check_extent(h, b, total, fn);
    b->heap_id = h->id;
    b->size = size;
    __atomic_store_n(&b->magic, RT_HEAP_BLOCK_MAGIC, __ATOMIC_RELEASE);
    return b + 1;
}

static BOOL block_free(RtHeap *h, DWORD flags, LPVOID p, const char *fn) {
    check_flags(fn, flags, FLAGS_FREE);
    if (!p) return TRUE;
    RtHeapBlock *b;
    BlockStatus s = block_check(h, p, &b);
    if (s != B_OK) bad_block(h, p, s, fn);
    uint64_t size = b->size;
    /* Clear the magic with a compare-and-swap, so that of two threads freeing the same block
     * exactly one gets past here and the other is reported, not handed to the zone. */
    uint32_t expect = RT_HEAP_BLOCK_MAGIC;
    if (!__atomic_compare_exchange_n(&b->magic, &expect, 0, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        bad_block(h, p, B_BAD_MAGIC, fn);
    if (h->maximum_size) release(h, charge(size));
    malloc_zone_free(h->zone, b);
    return TRUE;
}

/* ---- creating and destroying heaps ------------------------------------------------------------ */

/* Make a heap: claim a slot, a zone and an id.  NULL if the host refuses the zone.  More
 * than RT_HEAP_SLOTS heaps alive at once is fatal: each zone costs the host about 170 KB
 * before the first allocation, so a thousand live heaps is a leak, not a use. */
static RtHeap *heap_new(DWORD options, uint64_t initial, uint64_t maximum) {
    pthread_mutex_lock(&slab_lock);
    unsigned idx;
    bool fresh = slab_fresh < RT_HEAP_SLOTS;
    if (fresh) idx = slab_fresh;
    else if (dead_count) idx = dead_queue[dead_head];
    else rt_fatal("heap: %u heaps are alive at once, the most the runtime supports (RT_HEAP_SLOTS in "
                  "runtime/core/heap.c): HeapDestroy is not being called", RT_HEAP_SLOTS);
    if (last_id == UINT32_MAX) rt_fatal("heap: heap ids are exhausted after %u HeapCreate calls", UINT32_MAX);
    uint32_t id = last_id + 1;

    malloc_zone_t *zone = malloc_create_zone(0, 0);
    if (!zone) { pthread_mutex_unlock(&slab_lock); return NULL; }
    char name[32];
    if (id == 1) snprintf(name, sizeof name, "rt.heap.process");
    else snprintf(name, sizeof name, "rt.heap.%u", id);
    malloc_set_zone_name(zone, name);

    last_id = id;
    if (fresh) slab_fresh++;
    else { dead_head = (dead_head + 1) % RT_HEAP_SLOTS; dead_count--; }

    RtHeap *h = &heap_slab[idx];
    h->id = id;
    h->options = options;
    h->initial_size = initial;
    h->maximum_size = maximum;
    h->zone = zone;
    h->in_use = 0;
    __atomic_store_n(&h->magic, RT_HEAP_MAGIC, __ATOMIC_RELEASE);      /* publish last */
    pthread_mutex_unlock(&slab_lock);
    return h;
}

void rt_heap_init(void) {
    if (process_heap) rt_fatal("heap: rt_heap_init called twice");
    const RtMachine *mc = rt_machine();
    uint64_t lo = (uint64_t)(uintptr_t)heap_slab, hi = lo + sizeof heap_slab - 1;
    if (lo < mc->min_address || hi > mc->max_address)
        rt_fatal("heap: the heap header table is at %#llx..%#llx, outside the guest address range "
                 "%#llx..%#llx: heap handles would not be valid guest addresses",
                 ull(lo), ull(hi), ull(mc->min_address), ull(mc->max_address));
    /* Section 5.4: the process heap is growable and serialised (options 0). */
    RtHeap *h = heap_new(0, 0x1000, 0);
    if (!h) rt_fatal("heap: the host refused to create the malloc zone of the process heap");
    process_heap = h;
    /* GetProcessHeap's value, where guest code that reads the process block finds it. */
    ST64(rt_process_block() + RT_PEB_PROCESS_HEAP, (uint64_t)(uintptr_t)h);
}

HANDLE rt_heap_process(void) {
    if (!process_heap) rt_fatal("heap: GetProcessHeap before rt_heap_init");
    return process_heap;
}

HANDLE rt_heap_create(DWORD options, SIZE_T initial_size, SIZE_T maximum_size) {
    check_flags("HeapCreate", options, FLAGS_CREATE);
    if (maximum_size && initial_size > maximum_size)
        rt_fatal("heap: HeapCreate: the initial size %llu is larger than the maximum size %llu "
                 "(the documentation says it must be smaller; what Windows does with it is not known here)",
                 ull(initial_size), ull(maximum_size));
    const RtMachine *mc = rt_machine();
    if (initial_size > mc->max_address || maximum_size > mc->max_address) {
        /* Windows cannot reserve more than the address space.  UNSURE: which error code. */
        rt_log(RT_CH_VM, RT_LL_WARN, "heap: HeapCreate: initial size %llu or maximum size %llu is larger than "
               "the guest address space; failing with ERROR_NOT_ENOUGH_MEMORY", ull(initial_size), ull(maximum_size));
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (options & HEAP_CREATE_ENABLE_EXECUTE)
        rt_log_once(RT_CH_VM, RT_LL_WARN, "HeapCreate.ENABLE_EXECUTE",
                    "heap: HeapCreate(HEAP_CREATE_ENABLE_EXECUTE): accepted, but guest bytes are never executed "
                    "(docs/RUNTIME.md 5.2), so a call into memory from this heap is a dispatch miss");
    uint64_t page = mc->page_size;
    RtHeap *h = heap_new(options, round_up(initial_size ? initial_size : 1, page),
                         maximum_size ? round_up(maximum_size, page) : 0);
    if (!h) {
        rt_log(RT_CH_VM, RT_LL_WARN, "heap: HeapCreate: the host refused to create a malloc zone; failing with "
               "ERROR_NOT_ENOUGH_MEMORY");
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    return h;
}

BOOL rt_heap_destroy(HANDLE heap) {
    RtHeap *h = heap_ref(heap, "HeapDestroy");
    if (h == process_heap) {
        /* kernel32 HeapDestroy: ntdll refuses to destroy the process heap, and HeapDestroy
         * turns that into ERROR_INVALID_HANDLE (memory). */
        rt_thread_set_last_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    /* Retire the handle first, atomically, so that two threads destroying one heap cannot
     * both reach the zone. */
    uint32_t expect = RT_HEAP_MAGIC;
    if (!__atomic_compare_exchange_n(&h->magic, &expect, RT_HEAP_MAGIC_DEAD, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        bad_heap(heap, expect, true, "HeapDestroy");
    malloc_zone_t *zone = h->zone;
    malloc_destroy_zone(zone);                   /* every block of the heap goes with it */
    pthread_mutex_lock(&slab_lock);
    dead_queue[(dead_head + dead_count) % RT_HEAP_SLOTS] = (uint16_t)(h - heap_slab);
    dead_count++;
    pthread_mutex_unlock(&slab_lock);
    return TRUE;
}

/* ---- the Win32 operations --------------------------------------------------------------------- */

LPVOID rt_heap_alloc(HANDLE heap, DWORD flags, SIZE_T size) {
    RtHeap *h = heap_ref(heap, "HeapAlloc");
    check_flags("HeapAlloc", flags, FLAGS_ALLOC);
    LPVOID p = block_alloc(h, flags, size, "HeapAlloc");
    if (!p) rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);   /* Wine test_HeapCreate: a failed HeapAlloc sets it */
    return p;
}

LPVOID rt_heap_realloc(HANDLE heap, DWORD flags, LPVOID block, SIZE_T size) {
    static const char fn[] = "HeapReAlloc";
    RtHeap *h = heap_ref(heap, fn);
    check_flags(fn, flags, FLAGS_REALLOC);
    if (!block)
        rt_fatal("heap: %s: NULL block in heap %u (C realloc(NULL, n) is not HeapReAlloc; callers must "
                 "HeapAlloc instead)", fn, h->id);
    RtHeapBlock *b;
    BlockStatus s = block_check(h, block, &b);
    if (s != B_OK) bad_block(h, block, s, fn);

    uint64_t old_size = b->size;
    uint64_t usable = h->zone->size(h->zone, b);            /* header included: the slack a resize may use */
    uint64_t new_total = BLOCK_HEADER + size;
    bool in_place_only = (flags & HEAP_REALLOC_IN_PLACE_ONLY) != 0;

    if (too_big(size))
        return alloc_failed(h, flags, fn, size, "larger than any block the guest address space can hold");
    if (h->maximum_size && size >= FIXED_HEAP_BLOCK_LIMIT)
        return alloc_failed(h, flags, fn, size, "a heap created with a maximum size cannot hold a block of "
                            "0xFF000 bytes or more");

    if (size <= old_size) {
        /* Shrinking cannot fail.  The block stays where it is, except that one which would
         * give back at least half of what it holds, and 4 KB or more, is handed to the zone
         * to shrink (which may move it; not allowed with REALLOC_IN_PLACE_ONLY).  If the
         * zone cannot, the block stays. */
        if (!in_place_only && usable / 2 > new_total && usable - new_total >= 4096) {
            RtHeapBlock *m = malloc_zone_realloc(h->zone, b, new_total);
            if (m) { check_extent(h, m, new_total, fn); b = m; }
        }
        if (h->maximum_size) release(h, charge(old_size) - charge(size));
        b->size = size;
        return b + 1;
    }

    /* Growing. */
    uint64_t extra = 0;
    if (h->maximum_size) {
        extra = charge(size) - charge(old_size);
        if (!reserve(h, extra))
            return alloc_failed(h, flags, fn, size, "the heap was created with a maximum size and is full");
    }
    RtHeapBlock *nb = b;
    if (new_total > usable) {
        if (in_place_only) {
            if (extra) release(h, extra);
            return alloc_failed(h, flags, fn, size, "HEAP_REALLOC_IN_PLACE_ONLY and the block cannot grow in place");
        }
        nb = malloc_zone_realloc(h->zone, b, new_total);    /* contents kept; on failure the old block is intact */
        if (!nb) {
            if (extra) release(h, extra);
            return alloc_failed(h, flags, fn, size, "the host allocator refused");
        }
    }
    check_extent(h, nb, new_total, fn);                      /* also growth into slack: the extent changed */
    nb->size = size;
    /* HEAP_ZERO_MEMORY zeroes the growth, from the old requested size: the slack and a moved
     * block's copy of it hold whatever the zone had there. */
    if (flags & HEAP_ZERO_MEMORY) memset((char *)(nb + 1) + old_size, 0, size - old_size);
    return nb + 1;
}

BOOL rt_heap_free(HANDLE heap, DWORD flags, LPVOID block) {
    /* HeapFree(NULL, flags, NULL) is TRUE, and the handle is not looked at (Wine
     * kernel32/tests/heap.c test_HeapCreate).  The flags are still checked, as for any call. */
    if (!heap && !block) {
        check_flags("HeapFree", flags, FLAGS_FREE);
        return TRUE;
    }
    return block_free(heap_ref(heap, "HeapFree"), flags, block, "HeapFree");
}

SIZE_T rt_heap_size(HANDLE heap, DWORD flags, LPCVOID block) {
    RtHeap *h = heap_ref(heap, "HeapSize");
    check_flags("HeapSize", flags, FLAGS_FREE);
    RtHeapBlock *b;
    BlockStatus s = block_check(h, block, &b);
    if (s != B_OK) {
        /* The documented failure return.  Not fatal, unlike HeapFree: some code asks HeapSize
         * whether a pointer is a heap block. */
        rt_log(RT_CH_VM, RT_LL_DEBUG, "heap: HeapSize: %p is not a live block of heap %u (status %d); "
               "returning (SIZE_T)-1", block, h->id, (int)s);
        return (SIZE_T)-1;
    }
    return b->size;
}

BOOL rt_heap_validate(HANDLE heap, DWORD flags, LPCVOID block) {
    check_flags("HeapValidate", flags, FLAGS_FREE);
    /* HeapValidate docs: "If the specified heap or memory block is invalid, the return value
     * is zero."  So a bad heap handle is an answer here, not a report: the test of heap_ref
     * without bad_heap. */
    uint64_t off = (uint64_t)(uintptr_t)heap - (uint64_t)(uintptr_t)heap_slab;
    if (off >= sizeof heap_slab || off % sizeof(RtHeap) != 0) return FALSE;
    const RtHeap *h = (const RtHeap *)heap;
    if (__atomic_load_n(&h->magic, __ATOMIC_ACQUIRE) != RT_HEAP_MAGIC) return FALSE;
    /* Docs: "If this parameter is NULL, the function attempts to validate the entire heap".
     * There is no list of blocks to walk (rt/heap.h): a live heap is all that can be said. */
    if (!block) return TRUE;
    RtHeapBlock *b;
    return block_check(h, block, &b) == B_OK;
}

/* ---- the runtime's own use -------------------------------------------------------------------- */

void *rt_galloc(SIZE_T size) {
    if (!process_heap) rt_fatal("heap: rt_galloc(%llu) before rt_heap_init", ull(size));
    void *p = block_alloc(process_heap, HEAP_ZERO_MEMORY, size, "rt_galloc");
    if (!p) rt_fatal("heap: rt_galloc: out of memory: cannot allocate %llu bytes from the process heap", ull(size));
    return p;
}

void rt_gfree(void *p) {
    if (!process_heap) rt_fatal("heap: rt_gfree(%p) before rt_heap_init", p);
    block_free(process_heap, 0, p, "rt_gfree");
}
