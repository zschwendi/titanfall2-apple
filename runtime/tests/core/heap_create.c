/* HeapCreate / HeapDestroy, the flags every function takes, fixed-size heaps, and the ways an
 * allocation fails (docs/RUNTIME.md section 5.4; the Microsoft documentation of HeapCreate,
 * HeapDestroy, HeapAlloc, HeapReAlloc).  See heap.c for the rules that hold for every heap.
 */
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <stdlib.h>
#include <stdio.h>

#include "rt/test.h"
#include "rt/heap.h"
#include "rt/machine.h"

#define HEAP_CREATE_ENABLE_EXECUTE_ 0x00040000u         /* winnt.h; not in rt/heap.h */

static RtHeapBlock *hdr(const void *p) { return (RtHeapBlock *)p - 1; }

static bool all_zero(const void *p, uint64_t n) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

/* ---- HeapCreate / HeapDestroy ---------------------------------------------------------------- */

TEST(core, heap_create_destroy)
{
    HANDLE ph = rt_heap_process();
    HANDLE a = rt_heap_create(0, 0, 0);
    HANDLE b = rt_heap_create(0, 0, 0);
    T_REQUIRE(a != NULL && b != NULL);
    T_TRUE(a != b && a != ph && b != ph);
    for (unsigned i = 0; i < 2; i++) {
        HANDLE h = i ? b : a;
        T_EQ(*(uint32_t *)h, RT_HEAP_MAGIC);
        T_EQ((uint64_t)h & 15, 0);
        T_TRUE((uint64_t)h >= rt_machine()->min_address && (uint64_t)h <= rt_machine()->max_address);
    }

    /* Each heap stamps its own id into its blocks, and ids differ between heaps. */
    void *pa = rt_heap_alloc(a, 0, 10), *pb = rt_heap_alloc(b, 0, 10), *pp = rt_heap_alloc(ph, 0, 10);
    T_REQUIRE(pa && pb && pp);
    T_TRUE(hdr(pa)->heap_id != hdr(pb)->heap_id);
    T_TRUE(hdr(pa)->heap_id != hdr(pp)->heap_id);
    T_TRUE(hdr(pb)->heap_id != hdr(pp)->heap_id);
    T_TRUE(hdr(pa)->heap_id != 0 && hdr(pb)->heap_id != 0 && hdr(pp)->heap_id != 0);
    /* HeapSize is per heap: a block is a block of exactly one of them. */
    T_EQ(rt_heap_size(a, 0, pa), 10);
    T_EQ(rt_heap_size(b, 0, pa), (SIZE_T)-1);
    T_EQ(rt_heap_size(ph, 0, pa), (SIZE_T)-1);

    /* Destroying a heap with live blocks is allowed (that is what "destroyed whole" means) and
     * leaves the other heaps alone. */
    for (unsigned i = 0; i < 1000; i++) T_REQUIRE(rt_heap_alloc(a, 0, 1 + i * 13) != NULL);
    T_EQ(rt_heap_destroy(a), TRUE);
    T_EQ(rt_heap_size(b, 0, pb), 10);
    T_EQ(rt_heap_size(ph, 0, pp), 10);
    T_TRUE(rt_heap_alloc(b, 0, 10) != NULL);
    T_EQ(rt_heap_destroy(b), TRUE);
    T_EQ(rt_heap_size(ph, 0, pp), 10);
    T_EQ(rt_heap_free(ph, 0, pp), TRUE);

    /* A heap that is still empty can be destroyed straight away. */
    HANDLE c = rt_heap_create(0, 0, 0);
    T_REQUIRE(c != NULL);
    T_EQ(rt_heap_destroy(c), TRUE);
}

/* The process's physical footprint as the kernel counts it (resident, compressed and swapped). */
static uint64_t footprint(void) {
    task_vm_info_data_t info;
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
}

TEST(core, heap_destroy_gives_the_memory_back)
{
    /* "Destroyed whole": the zone and everything in it go back to the host, not just the handle.  The
     * host allocator keeps some freed large blocks mapped for reuse (the blocks stay mapped after
     * malloc_destroy_zone, so this cannot ask the kernel whether they are gone), so the test is that
     * 2 GiB of heaps, made, filled and destroyed one after another, do not add up to 2 GiB of footprint. */
    uint64_t base = footprint();
    T_REQUIRE(base != 0);
    enum { ROUNDS = 64, BLOCKS = 4, SIZE = 8 << 20 };
    for (unsigned round = 0; round < ROUNDS; round++) {
        HANDLE h = rt_heap_create(0, 0, 0);
        T_REQUIRE(h != NULL);
        for (unsigned i = 0; i < BLOCKS; i++) {
            uint8_t *p = rt_heap_alloc(h, 0, SIZE);
            T_REQUIRE(p != NULL);
            memset(p, 0xA5, SIZE);                                /* touch every page */
        }
        T_EQ(rt_heap_destroy(h), TRUE);
    }
    T_TRUE(footprint() < base + (UINT64_C(512) << 20));           /* a leak of every heap would be 2 GiB */
}

TEST(core, heap_destroy_is_not_a_free_of_other_heaps)
{
    /* Many heaps at once, each with its own blocks; destroy them in a different order from
     * creation; every survivor keeps all its blocks, intact. */
    enum { N = 64 };
    HANDLE h[N];
    uint8_t *blk[N][8];
    for (unsigned i = 0; i < N; i++) {
        h[i] = rt_heap_create(i & 1 ? HEAP_NO_SERIALIZE : 0, 0, 0);
        T_REQUIRE(h[i] != NULL);
        for (unsigned j = 0; j < i; j++) T_REQUIRE(h[i] != h[j]);
        for (unsigned j = 0; j < 8; j++) {
            blk[i][j] = rt_heap_alloc(h[i], 0, 100 + j);
            T_REQUIRE(blk[i][j] != NULL);
            memset(blk[i][j], (int)(i + j), 100 + j);
        }
    }
    for (unsigned k = 0; k < N; k += 2) {                           /* the even ones first, then... */
        T_EQ(rt_heap_destroy(h[k]), TRUE);
    }
    for (unsigned i = 1; i < N; i += 2) {
        for (unsigned j = 0; j < 8; j++) {
            T_EQ(rt_heap_size(h[i], 0, blk[i][j]), 100 + j);
            for (unsigned x = 0; x < 100 + j; x++) T_REQUIRE(blk[i][j][x] == (uint8_t)(i + j));
        }
    }
    for (unsigned i = 1; i < N; i += 2) T_EQ(rt_heap_destroy(h[i]), TRUE);   /* ...then the odd ones */
}

TEST(core, heap_slots_are_reused_but_not_at_once)
{
    /* More create/destroy pairs than the table has slots (1024): every one works, ids are
     * never reused, and a handle just destroyed is not handed out again straight away, so
     * use of it is still caught. */
    uint32_t last = 0;
    HANDLE prev = NULL;
    for (unsigned i = 0; i < 3300; i++) {
        HANDLE h = rt_heap_create(0, 0, 0);
        T_REQUIRE(h != NULL);
        T_REQUIRE(h != prev);
        void *p = rt_heap_alloc(h, 0, 1);
        T_REQUIRE(p != NULL);
        T_REQUIRE(hdr(p)->heap_id > last);                          /* strictly increasing: never reused */
        last = hdr(p)->heap_id;
        T_REQUIRE(rt_heap_destroy(h) == TRUE);
        prev = h;
    }
    T_TRAPS(rt_heap_alloc(prev, 0, 1), "belongs to a heap that HeapDestroy already destroyed");
}

TEST(core, heap_too_many_live_heaps)
{
    /* 1024 heaps alive at once is the most there is room for (the process heap is one of them).
     * Past that the runtime stops and says so; it does not return NULL, which would look like
     * an out-of-memory error that Windows never gives for this. */
    T_TRAPS({ for (unsigned i = 0; i < 1100; i++) (void)rt_heap_create(0, 0, 0); },
            "1024 heaps are alive at once");
    /* Destroyed heaps free their slots: 1023 live ones, destroyed, and again. */
    enum { N = 1023 };
    static HANDLE h[N];
    for (unsigned round = 0; round < 2; round++) {
        for (unsigned i = 0; i < N; i++) { h[i] = rt_heap_create(0, 0, 0); T_REQUIRE(h[i] != NULL); }
        for (unsigned i = 0; i < N; i++) T_REQUIRE(rt_heap_destroy(h[i]) == TRUE);
    }
}

TEST(core, heap_destroy_and_handle_errors)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    void *p = rt_heap_alloc(h, 0, 5);
    T_REQUIRE(p != NULL);
    T_EQ(rt_heap_destroy(h), TRUE);

    /* Every operation on a destroyed heap is fatal, and says so. */
    T_TRAPS(rt_heap_destroy(h), "heap: HeapDestroy: heap handle");
    T_TRAPS(rt_heap_destroy(h), "already destroyed");
    T_TRAPS(rt_heap_alloc(h, 0, 1), "heap: HeapAlloc: heap handle");
    T_TRAPS(rt_heap_realloc(h, 0, p, 1), "heap: HeapReAlloc: heap handle");
    T_TRAPS(rt_heap_free(h, 0, p), "heap: HeapFree: heap handle");
    T_TRAPS(rt_heap_free(h, 0, NULL), "already destroyed");     /* even a NULL block: the heap is checked first */
    T_TRAPS(rt_heap_size(h, 0, p), "heap: HeapSize: heap handle");

    /* Things that are not heaps at all. */
    T_TRAPS(rt_heap_alloc(NULL, 0, 1), "the heap handle is NULL");
    T_TRAPS(rt_heap_destroy(NULL), "the heap handle is NULL");
    /* HeapFree(NULL, 0, NULL) is TRUE (Windows: Wine kernel32/tests/heap.c test_HeapCreate,
     * "test some border cases").  A NULL block with a handle that is not NULL is still checked
     * first, so a NULL handle with a real block is fatal. */
    T_EQ(rt_heap_free(NULL, 0, NULL), TRUE);
    T_TRAPS(rt_heap_free(NULL, 0, p), "the heap handle is NULL");
    T_TRAPS(rt_heap_size(NULL, 0, p), "the heap handle is NULL");
    T_TRAPS(rt_heap_realloc(NULL, 0, p, 1), "the heap handle is NULL");
    T_TRAPS(rt_heap_alloc((HANDLE)0x44, 0, 1), "0x44 is not a heap handle");     /* a handle-table value */
    int on_stack = 0;
    T_TRAPS(rt_heap_alloc(&on_stack, 0, 1), "is not a heap handle");
    uint64_t fake[16] __attribute__((aligned(64))) = {RT_HEAP_MAGIC};
    T_TRAPS(rt_heap_alloc(fake, 0, 1), "is not a heap handle");                 /* the magic alone is not enough */
    T_TRAPS(rt_heap_alloc((char *)ph + 1, 0, 1), "is not a heap handle");           /* inside a header */
    T_TRAPS(rt_heap_alloc((char *)ph + 8, 0, 1), "is not a heap handle");
    void *block = rt_heap_alloc(ph, 0, 64);
    T_REQUIRE(block != NULL);
    T_TRAPS(rt_heap_alloc(block, 0, 1), "is not a heap handle");                 /* a block is not a heap */
    T_EQ(rt_heap_free(ph, 0, block), TRUE);
    /* The next header slot in the table was never used. */
    T_TRAPS(rt_heap_alloc((char *)ph + 64 * 1000, 0, 1), "is an unused heap header");
    T_TRAPS(rt_heap_alloc((char *)ph + 64 * 1024, 0, 1), "is not a heap handle");  /* one past the table */
    T_TRAPS(rt_heap_alloc((char *)ph - 64, 0, 1), "is not a heap handle");         /* one before it */
}

/* ---- options ------------------------------------------------------------------------------------ */

TEST(core, heap_create_options)
{
    unsigned logged_before = T_LOG_COUNT(RT_CH_VM);
    static const DWORD good[] = {
        0, HEAP_NO_SERIALIZE, HEAP_GENERATE_EXCEPTIONS, HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS,
        HEAP_CREATE_ENABLE_EXECUTE_, HEAP_CREATE_ENABLE_EXECUTE_ | HEAP_NO_SERIALIZE,
        HEAP_CREATE_ENABLE_EXECUTE_ | HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS
    };
    for (unsigned i = 0; i < sizeof good / sizeof good[0]; i++) {
        HANDLE h = rt_heap_create(good[i], 0, 0);
        T_REQUIRE(h != NULL);
        void *p = rt_heap_alloc(h, HEAP_ZERO_MEMORY, 200);
        T_REQUIRE(p != NULL);
        T_TRUE(all_zero(p, 200));
        T_EQ(rt_heap_free(h, 0, p), TRUE);
        T_EQ(rt_heap_destroy(h), TRUE);
    }
    /* Execute permission cannot be given (guest bytes are never run): said once, not silently. */
    T_TRUE(T_LOGGED(RT_CH_VM, "HEAP_CREATE_ENABLE_EXECUTE"));
    T_EQ(T_LOG_COUNT(RT_CH_VM), logged_before + 1);                 /* once, not once per heap */

    /* Every other bit stops the run and names the value: the flags of the other heap functions
     * (HEAP_ZERO_MEMORY, HEAP_REALLOC_IN_PLACE_ONLY), HEAP_GROWABLE (0x2), the debugging flags, and the rest. */
    static const DWORD bad[] = {
        HEAP_ZERO_MEMORY, HEAP_REALLOC_IN_PLACE_ONLY, 0x2, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x10000, 0x20000,
        0x80000, 0x80000000u, 0xFFFFFFFFu, HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        T_TRAPS(rt_heap_create(bad[i], 0, 0), "heap: HeapCreate: flags");
    T_TRAPS(rt_heap_create(HEAP_ZERO_MEMORY, 0, 0), "flags 0x8 contain 0x8, which this function does not take");
}

TEST(core, heap_create_sizes)
{
    /* The initial size is a hint and the maximum, if nonzero, makes the heap fixed-size.  Every
     * pair that Windows accepts works, whatever the sizes are. */
    static const struct { SIZE_T initial, maximum; } ok[] = {
        {0, 0}, {1, 0}, {0x1000, 0}, {0x1001, 0}, {1u << 20, 0}, {UINT64_C(1) << 40, 0},
        {0, 1}, {0, 0x1000}, {0, 0x1001}, {0, 1u << 20}, {0, UINT64_C(1) << 36},
        {1, 1}, {0x1000, 0x1000}, {1, 0x1000}, {0x1000, 0x2000}, {1u << 20, 1u << 20}
    };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        HANDLE h = rt_heap_create(0, ok[i].initial, ok[i].maximum);
        T_REQUIRE(h != NULL);
        void *p = rt_heap_alloc(h, HEAP_ZERO_MEMORY, 100);
        T_REQUIRE(p != NULL);
        T_TRUE(all_zero(p, 100));
        T_EQ(rt_heap_size(h, 0, p), 100);
        T_EQ(rt_heap_free(h, 0, p), TRUE);
        T_EQ(rt_heap_destroy(h), TRUE);
    }
    /* The documentation says the initial size must be smaller than the maximum; what Windows
     * does with a larger one is not known, so it is a stop, not a guess.  Equal is fine (above). */
    T_TRAPS(rt_heap_create(0, 0x2000, 0x1000), "initial size 8192 is larger than the maximum size 4096");
    T_TRAPS(rt_heap_create(0, 2, 1), "initial size 2 is larger than the maximum size 1");
    T_TRAPS(rt_heap_create(0, (SIZE_T)-1, 1), "is larger than the maximum size 1");
}

TEST(core, heap_create_fails_beyond_the_address_space)
{
    /* A heap bigger than the 8 TB the guest has cannot exist: HeapCreate returns NULL with
     * ERROR_NOT_ENOUGH_MEMORY (the code is the unit's best knowledge of kernel32, not verified). */
    uint64_t limit = rt_machine()->max_address;
    HANDLE ok = rt_heap_create(0, limit, 0);                         /* exactly the limit still works */
    T_REQUIRE(ok != NULL);
    T_EQ(rt_heap_destroy(ok), TRUE);
    ok = rt_heap_create(0, 0, limit);
    T_REQUIRE(ok != NULL);
    T_EQ(rt_heap_destroy(ok), TRUE);

    static const uint64_t over[] = {0, 1, 2, UINT64_C(1) << 20, UINT64_C(1) << 40};
    for (unsigned i = 0; i < sizeof over / sizeof over[0]; i++) {
        uint64_t big = limit + 1 + over[i];
        rt_thread_set_last_error(0);
        T_TRUE(rt_heap_create(0, big, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
        rt_thread_set_last_error(0);
        T_TRUE(rt_heap_create(0, 0, big) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    }
    rt_thread_set_last_error(0);
    T_TRUE(rt_heap_create(0, 0, (SIZE_T)-1) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    rt_thread_set_last_error(0);
    T_TRUE(rt_heap_create(0, (SIZE_T)-1, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_TRUE(T_LOGGED(RT_CH_VM, "larger than the guest address space; failing with ERROR_NOT_ENOUGH_MEMORY"));
    /* Success leaves the last error alone. */
    rt_thread_set_last_error(0x3333);
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x3333);
    T_EQ(rt_heap_destroy(h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x3333);
}

/* ---- the flags of every function ---------------------------------------------------------------- */

enum { FN_CREATE, FN_ALLOC, FN_REALLOC, FN_FREE, FN_SIZE, FN_COUNT };
static const char *const fn_names[FN_COUNT] = {"HeapCreate", "HeapAlloc", "HeapReAlloc", "HeapFree", "HeapSize"};

/* One call of function `fn` with `flags` on fresh state; true if it did what it should. */
static bool call_with_flags(int fn, DWORD flags) {
    HANDLE ph = rt_heap_process();
    switch (fn) {
    case FN_CREATE: {
        HANDLE h = rt_heap_create(flags, 0, 0);
        return h != NULL && rt_heap_destroy(h) == TRUE;
    }
    case FN_ALLOC: {
        uint8_t *p = rt_heap_alloc(ph, flags, 16);
        bool ok = p != NULL && ((flags & HEAP_ZERO_MEMORY) == 0 || all_zero(p, 16));
        return ok && rt_heap_free(ph, 0, p) == TRUE;
    }
    case FN_REALLOC: {
        uint8_t *p = rt_heap_alloc(ph, 0, 64);
        memset(p, 0xEE, 64);
        uint8_t *q = rt_heap_realloc(ph, flags, p, 32);               /* a shrink: allowed in place too */
        bool ok = q != NULL && rt_heap_size(ph, 0, q) == 32;
        return ok && rt_heap_free(ph, 0, q) == TRUE;
    }
    case FN_FREE: {
        void *p = rt_heap_alloc(ph, 0, 16);
        return rt_heap_free(ph, flags, p) == TRUE;
    }
    default: {
        void *p = rt_heap_alloc(ph, 0, 16);
        bool ok = rt_heap_size(ph, flags, p) == 16;
        return ok && rt_heap_free(ph, 0, p) == TRUE;
    }
    }
}

TEST(core, heap_flag_matrix)
{
    /* Every single bit, for every function: the bits the documentation gives that function work,
     * every other bit stops the run with a report that names the function and the value. */
    static const DWORD takes[FN_COUNT] = {
        [FN_CREATE]  = HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_CREATE_ENABLE_EXECUTE_,
        [FN_ALLOC]   = HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY,
        [FN_REALLOC] = HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | HEAP_REALLOC_IN_PLACE_ONLY,
        [FN_FREE]    = HEAP_NO_SERIALIZE,
        [FN_SIZE]    = HEAP_NO_SERIALIZE,
    };
    for (int fn = 0; fn < FN_COUNT; fn++) {
        for (unsigned bit = 0; bit < 32; bit++) {
            DWORD f = (DWORD)1 << bit;
            if (takes[fn] & f) {
                T_TRUE(call_with_flags(fn, f));
            } else {
                char want[96];
                snprintf(want, sizeof want, "heap: %s: flags %#x contain %#x, which this function does not take",
                         fn_names[fn], f, f);
                T_TRAPS((void)call_with_flags(fn, f), want);
            }
        }
        /* All the accepted bits at once, and with one rejected bit among them. */
        T_TRUE(call_with_flags(fn, takes[fn]));
        T_TRUE(call_with_flags(fn, 0));
        T_TRAPS((void)call_with_flags(fn, takes[fn] | 0x80000000u), "does not take");
        T_TRAPS((void)call_with_flags(fn, 0xFFFFFFFFu), "does not take");
    }
    /* Every subset of the flags HeapAlloc and HeapReAlloc take. */
    HANDLE ph = rt_heap_process();
    for (DWORD f = 0; f < 16; f++) {
        DWORD flags = (f & 1 ? HEAP_NO_SERIALIZE : 0) | (f & 2 ? HEAP_GENERATE_EXCEPTIONS : 0) |
                      (f & 4 ? HEAP_ZERO_MEMORY : 0);
        uint8_t *p = rt_heap_alloc(ph, flags, 100);
        T_REQUIRE(p != NULL);
        memset(p, 0xEE, 100);
        uint8_t *q = rt_heap_realloc(ph, flags | (f & 8 ? HEAP_REALLOC_IN_PLACE_ONLY : 0), p, 90);
        T_REQUIRE(q != NULL);
        T_EQ(rt_heap_size(ph, 0, q), 90);
        T_EQ(rt_heap_free(ph, flags & HEAP_NO_SERIALIZE, q), TRUE);
    }
}

TEST(core, heap_flag_error_names_the_value)
{
    HANDLE ph = rt_heap_process();
    T_TRAPS(rt_heap_alloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, 8), "flags 0x10 contain 0x10, which this function does not take (it takes 0xd)");
    T_TRAPS(rt_heap_alloc(ph, 0x1234, 8), "flags 0x1234 contain 0x1230");
    T_TRAPS(rt_heap_free(ph, HEAP_ZERO_MEMORY, NULL), "heap: HeapFree: flags 0x8 contain 0x8");     /* even with a NULL block */
    T_TRAPS(rt_heap_size(ph, HEAP_GENERATE_EXCEPTIONS, NULL), "heap: HeapSize: flags 0x4 contain 0x4");
    void *p = rt_heap_alloc(ph, 0, 8);
    T_REQUIRE(p != NULL);
    T_TRAPS(rt_heap_realloc(ph, 0x20, p, 8), "heap: HeapReAlloc: flags 0x20 contain 0x20");
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

/* ---- fixed-size heaps (HeapCreate with a nonzero maximum) --------------------------------------- */

/* How many blocks of `size` bytes does `h` take before it says NULL? */
static unsigned fill_until_full(HANDLE h, uint64_t size, void **blk, unsigned cap) {
    unsigned n = 0;
    while (n < cap) {
        void *p = rt_heap_alloc(h, 0, size);
        if (!p) break;
        blk[n++] = p;
    }
    return n;
}

TEST(core, heap_fixed_size_capacity)
{
    /* A block costs 16 + its size rounded up to 16, and the heap holds its maximum rounded up
     * to a 4 KB page.  So 64 KB holds exactly 1024 blocks of 48 bytes (64 each), 2048 of 1..16
     * bytes (32 each, 16 + 16) and 4096 of none (16 each). */
    static const struct { uint64_t maximum, size; } cases[] = {
        {65536, 48}, {65536, 17}, {65536, 1}, {65536, 16}, {65536, 0}, {1, 16}, {4096, 16}, {4097, 16}, {8192, 16},
        {4096, 0}, {4096, 1008}, {4096, 1009}, {4096, 4080}, {4096, 4081}, {4096, 4000}, {100000, 100},
    };
    static void *blk[8192];
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        uint64_t cost = 16 + ((cases[c].size + 15) & ~UINT64_C(15));
        uint64_t max_rounded = (cases[c].maximum + 4095) & ~UINT64_C(4095);
        uint64_t expect = max_rounded / cost;
        HANDLE h = rt_heap_create(0, 0, cases[c].maximum);
        T_REQUIRE(h != NULL);
        for (unsigned round = 0; round < 3; round++) {              /* freeing gives the room back, exactly */
            unsigned n = fill_until_full(h, cases[c].size, blk, 8192);
            T_EQ(n, expect);
            for (unsigned i = 0; i < n; i++) T_EQ(rt_heap_size(h, 0, blk[i]), cases[c].size);
            T_TRUE(rt_heap_alloc(h, 0, cases[c].size) == NULL);     /* still full */
            if (n) {
                T_EQ(rt_heap_free(h, 0, blk[n - 1]), TRUE);
                void *again = rt_heap_alloc(h, HEAP_ZERO_MEMORY, cases[c].size);
                T_REQUIRE(again != NULL);                           /* exactly one block's worth came back */
                T_TRUE(all_zero(again, cases[c].size));
                T_TRUE(rt_heap_alloc(h, 0, cases[c].size) == NULL);
                blk[n - 1] = again;
            }
            for (unsigned i = 0; i < n; i++) T_EQ(rt_heap_free(h, 0, blk[i]), TRUE);
        }
        T_EQ(rt_heap_destroy(h), TRUE);
    }
}

TEST(core, heap_fixed_size_block_limit)
{
    /* In a heap that cannot grow, one block must be smaller than 0xFF000 bytes, however big the
     * heap is.  That figure is OURS (rt/heap.h): Windows is known to take a block of 0x80000 and
     * refuse one of 0x100000 (Wine kernel32/tests/heap.c test_HeapCreate), and the exact limit is
     * not.  A heap that grows takes any size, which heap_block_layout covers. */
    HANDLE h = rt_heap_create(0, 0, UINT64_C(1) << 30);
    T_REQUIRE(h != NULL);
    rt_thread_set_last_error(0x6161);
    void *a = rt_heap_alloc(h, 0, 0xFEFFF);
    T_REQUIRE(a != NULL);
    T_EQ(rt_heap_size(h, 0, a), 0xFEFFF);
    T_TRUE(rt_heap_alloc(h, 0, 0xFF000) == NULL);
    T_TRUE(rt_heap_alloc(h, 0, 0xFF001) == NULL);
    T_TRUE(rt_heap_alloc(h, HEAP_ZERO_MEMORY, 1u << 20) == NULL);
    T_TRUE(rt_heap_alloc(h, 0, UINT64_C(1) << 31) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);                 /* a failed HeapAlloc */
    T_TRUE(T_LOGGED(RT_CH_VM, "a heap created with a maximum size cannot hold a block of 0xFF000 bytes or more"));

    /* Growing a block past the limit fails the same way and leaves the block alone; shrinking
     * from a size within the limit is always fine.  HeapReAlloc leaves last error alone. */
    void *b = rt_heap_alloc(h, 0, 1000);
    T_REQUIRE(b != NULL);
    rt_thread_set_last_error(0x6161);
    T_TRUE(rt_heap_realloc(h, 0, b, 0xFF000) == NULL);
    T_TRUE(rt_heap_realloc(h, HEAP_ZERO_MEMORY, b, 1u << 22) == NULL);
    T_EQ(rt_heap_size(h, 0, b), 1000);
    b = rt_heap_realloc(h, 0, b, 0xFEFFF);
    T_REQUIRE(b != NULL);
    T_EQ(rt_heap_size(h, 0, b), 0xFEFFF);
    b = rt_heap_realloc(h, 0, b, 10);
    T_REQUIRE(b != NULL);
    T_EQ(rt_heap_size(h, 0, b), 10);
    T_EQ(T_LAST_ERROR(), 0x6161);
    T_EQ(rt_heap_free(h, 0, a), TRUE);
    T_EQ(rt_heap_free(h, 0, b), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_fixed_size_realloc_accounting)
{
    /* Four blocks of 1008 bytes (1024 each) fill a 4096-byte heap.  Reallocation is charged
     * the difference, and a growth that does not fit fails with the block untouched. */
    HANDLE h = rt_heap_create(0, 0, 4096);
    T_REQUIRE(h != NULL);
    uint8_t *b[4];
    for (unsigned i = 0; i < 4; i++) {
        b[i] = rt_heap_alloc(h, 0, 1008);
        T_REQUIRE(b[i] != NULL);
        memset(b[i], 0x30 + (int)i, 1008);
    }
    T_TRUE(rt_heap_alloc(h, 0, 0) == NULL);                          /* full */
    rt_thread_set_last_error(0x8181);
    T_TRUE(rt_heap_realloc(h, 0, b[0], 1009) == NULL);               /* 1040 > 1024: 16 more than there is */
    T_TRUE(rt_heap_realloc(h, HEAP_ZERO_MEMORY, b[1], 1u << 15) == NULL);
    T_EQ(T_LAST_ERROR(), 0x8181);
    T_EQ(rt_heap_size(h, 0, b[0]), 1008);
    for (unsigned x = 0; x < 1008; x++) T_REQUIRE(b[0][x] == 0x30);
    /* The same size (1008) and any size up to 1008 still reallocates. */
    T_TRUE(rt_heap_realloc(h, 0, b[0], 1008) != NULL);
    b[0] = rt_heap_realloc(h, 0, b[0], 1000);                        /* 1000 rounds to 1008: costs the same */
    T_REQUIRE(b[0] != NULL);
    T_TRUE(rt_heap_alloc(h, 0, 0) == NULL);
    /* Shrinking gives room back: 1008 -> 500 returns 1024 - 528 = 496 bytes. */
    b[0] = rt_heap_realloc(h, 0, b[0], 500);
    T_REQUIRE(b[0] != NULL);
    void *x = rt_heap_alloc(h, 0, 480);                              /* 496 exactly */
    T_REQUIRE(x != NULL);
    T_TRUE(rt_heap_alloc(h, 0, 0) == NULL);
    T_EQ(rt_heap_free(h, 0, x), TRUE);
    T_TRUE(rt_heap_alloc(h, 0, 481) == NULL);                        /* 512 > 496 */
    x = rt_heap_alloc(h, 0, 470);
    T_REQUIRE(x != NULL);
    /* Growing back to the original size takes exactly the room that was given back, while it is free. */
    T_EQ(rt_heap_free(h, 0, x), TRUE);
    b[0] = rt_heap_realloc(h, HEAP_ZERO_MEMORY, b[0], 1008);
    T_REQUIRE(b[0] != NULL);
    T_TRUE(all_zero(b[0] + 500, 508));
    T_TRUE(rt_heap_alloc(h, 0, 0) == NULL);
    /* Freeing everything gives all 4096 back; one block of 4080 (4096 charged) then fits. */
    for (unsigned i = 0; i < 4; i++) T_EQ(rt_heap_free(h, 0, b[i]), TRUE);
    x = rt_heap_alloc(h, 0, 4080);
    T_REQUIRE(x != NULL);
    T_TRUE(rt_heap_alloc(h, 0, 0) == NULL);
    /* A failed realloc to a huge size did not leak a reservation either. */
    T_TRUE(rt_heap_realloc(h, 0, x, 1u << 20) == NULL);
    T_TRUE(rt_heap_realloc(h, 0, x, 4081) == NULL);
    x = rt_heap_realloc(h, 0, x, 1);
    T_REQUIRE(x != NULL);
    T_EQ(rt_heap_free(h, 0, x), TRUE);
    x = rt_heap_alloc(h, 0, 4080);
    T_REQUIRE(x != NULL);
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_fixed_size_in_place_growth_is_charged)
{
    /* Growth inside a block's own room needs nothing new from the zone, but it is still charged
     * to the heap's maximum.  The zone rounds a 1100-byte block up to 1536 bytes (small blocks
     * go in 512-byte steps), so growing it to its room costs 416 more than it holds. */
    HANDLE h = rt_heap_create(0, 0, 4096);
    T_REQUIRE(h != NULL);
    uint8_t *p = rt_heap_alloc(h, 0, 1100);
    T_REQUIRE(p != NULL);
    uint64_t room = malloc_size(p - sizeof(RtHeapBlock)) - sizeof(RtHeapBlock);
    T_REQUIRE(room > 1100 + 16);                                  /* the assumption about the zone */
    uint64_t cost_now = 16 + 1104, cost_room = 16 + room, extra = cost_room - cost_now;
    T_REQUIRE(extra % 16 == 0 && extra >= 32);
    /* Leave the heap `extra - 16` bytes short of what the growth needs. */
    uint64_t filler_cost = 4096 - cost_now - (extra - 16);
    void *filler = rt_heap_alloc(h, 0, filler_cost - 16);
    T_REQUIRE(filler != NULL);
    T_TRUE(rt_heap_realloc(h, HEAP_REALLOC_IN_PLACE_ONLY, p, room) == NULL);    /* does not fit the maximum */
    T_EQ(rt_heap_size(h, 0, p), 1100);
    T_EQ(rt_heap_free(h, 0, filler), TRUE);
    T_TRUE(rt_heap_realloc(h, HEAP_REALLOC_IN_PLACE_ONLY, p, room) == p);        /* now it does */
    T_EQ(rt_heap_size(h, 0, p), room);
    /* And the room is charged: what is left is exactly 4096 - cost_room, no more. */
    uint64_t left = 4096 - cost_room;
    T_TRUE(rt_heap_alloc(h, 0, left - 16 + 1) == NULL);
    void *rest = rt_heap_alloc(h, 0, left - 16);
    T_TRUE(rest != NULL);
    T_EQ(rt_heap_free(h, 0, rest), TRUE);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_fixed_size_zero_and_flags)
{
    HANDLE h = rt_heap_create(HEAP_NO_SERIALIZE, 4096, 8192);
    T_REQUIRE(h != NULL);
    uint8_t *p = rt_heap_alloc(h, HEAP_ZERO_MEMORY, 3000);
    T_REQUIRE(p != NULL);
    T_TRUE(all_zero(p, 3000));
    memset(p, 0xEE, 3000);
    p = rt_heap_realloc(h, HEAP_ZERO_MEMORY, p, 3500);
    T_REQUIRE(p != NULL);
    T_TRUE(all_zero(p + 3000, 500));
    T_EQ(rt_heap_free(h, HEAP_NO_SERIALIZE, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
}

/* ---- HEAP_GENERATE_EXCEPTIONS ------------------------------------------------------------------- */

TEST(core, heap_generate_exceptions)
{
    HANDLE ph = rt_heap_process();
    HANDLE ex = rt_heap_create(HEAP_GENERATE_EXCEPTIONS, 0, 0);
    HANDLE fixed = rt_heap_create(HEAP_GENERATE_EXCEPTIONS, 0, 4096);
    T_REQUIRE(ex != NULL && fixed != NULL);

    /* Without the flag a failure is NULL (checked throughout); with it, in the call or in the
     * heap's creation flags, it is the exception Windows raises, which is a stop here
     * (exceptions are plan A, docs/RUNTIME.md 9.3).  The report names the heap and the size. */
    T_TRUE(rt_heap_alloc(ph, 0, UINT64_C(1) << 62) == NULL);
    T_TRAPS(rt_heap_alloc(ph, HEAP_GENERATE_EXCEPTIONS, UINT64_C(1) << 62),
            "request for 4611686018427387904 bytes (larger than any block the guest address space can hold), and HEAP_GENERATE_EXCEPTIONS is set");
    T_TRAPS(rt_heap_alloc(ph, HEAP_GENERATE_EXCEPTIONS, UINT64_C(1) << 62), "STATUS_NO_MEMORY (0xC0000017)");
    T_TRAPS(rt_heap_alloc(ph, HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | HEAP_NO_SERIALIZE, (SIZE_T)-1),
            "HEAP_GENERATE_EXCEPTIONS is set");
    T_TRAPS(rt_heap_alloc(ex, 0, UINT64_C(1) << 62), "heap: HeapAlloc: heap");       /* the heap's own flag */
    T_TRAPS(rt_heap_alloc(ex, HEAP_ZERO_MEMORY, UINT64_C(1) << 50), "HEAP_GENERATE_EXCEPTIONS is set");
    T_TRAPS(rt_heap_alloc(ex, 0, rt_machine()->max_address), "HEAP_GENERATE_EXCEPTIONS is set");

    /* Reallocation: the block is untouched in the parent, and the child stops. */
    uint8_t *p = rt_heap_alloc(ex, 0, 100);
    uint8_t *q = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL && q != NULL);
    T_TRAPS(rt_heap_realloc(ex, 0, p, UINT64_C(1) << 62), "heap: HeapReAlloc: heap");
    T_TRAPS(rt_heap_realloc(ph, HEAP_GENERATE_EXCEPTIONS, q, UINT64_C(1) << 62), "HEAP_GENERATE_EXCEPTIONS is set");
    T_TRAPS(rt_heap_realloc(ph, HEAP_GENERATE_EXCEPTIONS | HEAP_REALLOC_IN_PLACE_ONLY, q, 1u << 20),
            "HEAP_REALLOC_IN_PLACE_ONLY and the block cannot grow in place");
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, q, 1u << 20) == NULL);   /* the same request, no exception flag */
    T_EQ(rt_heap_size(ex, 0, p), 100);
    T_EQ(rt_heap_size(ph, 0, q), 100);

    /* A fixed-size heap that is full, or asked for a block over its limit. */
    void *f = rt_heap_alloc(fixed, 0, 4000);
    T_REQUIRE(f != NULL);
    T_TRAPS(rt_heap_alloc(fixed, 0, 200), "the heap was created with a maximum size and is full");
    T_TRAPS(rt_heap_alloc(fixed, 0, 0xFF000), "cannot hold a block of 0xFF000 bytes or more");
    T_TRAPS(rt_heap_realloc(fixed, 0, f, 5000), "HEAP_GENERATE_EXCEPTIONS is set");

    /* The flag changes nothing when nothing fails. */
    uint8_t *z = rt_heap_alloc(ex, HEAP_ZERO_MEMORY, 300);
    T_REQUIRE(z != NULL);
    T_TRUE(all_zero(z, 300));
    z = rt_heap_realloc(ex, HEAP_ZERO_MEMORY, z, 600);
    T_REQUIRE(z != NULL);
    T_TRUE(all_zero(z, 600));
    T_EQ(rt_heap_free(ex, 0, z), TRUE);
    T_EQ(rt_heap_free(ex, 0, p), TRUE);
    T_EQ(rt_heap_free(ph, 0, q), TRUE);
    T_EQ(rt_heap_free(fixed, 0, f), TRUE);
    T_EQ(rt_heap_destroy(ex), TRUE);
    T_EQ(rt_heap_destroy(fixed), TRUE);
}

TEST(core, heap_galloc_exhaustion_is_fatal)
{
    /* rt_galloc is for the runtime's own use: "fatal when memory is exhausted". */
    T_TRAPS(rt_galloc(UINT64_C(1) << 62), "rt_galloc: out of memory: cannot allocate 4611686018427387904 bytes");
    T_TRAPS(rt_galloc((SIZE_T)-1), "rt_galloc: out of memory");
    T_TRAPS(rt_galloc(rt_machine()->max_address), "rt_galloc: out of memory");
}

/* ---- allocation failure ------------------------------------------------------------------------- */

TEST(core, heap_alloc_failure_boundaries)
{
    /* Impossible sizes return NULL cleanly: nothing wraps around when the 16-byte header is
     * added, nothing is truncated to 32 bits, no last error is set, and the heap works afterwards. */
    HANDLE ph = rt_heap_process();
    uint64_t limit = rt_machine()->max_address;
    static const int64_t deltas[] = {0, 1, 2, 8, 15, 16, 17, 31, 32, 33, 4095, 4096, 65535};
    rt_thread_set_last_error(0x9999);
    for (unsigned i = 0; i < sizeof deltas / sizeof deltas[0]; i++) {
        uint64_t d = (uint64_t)deltas[i];
        T_TRUE(rt_heap_alloc(ph, 0, (SIZE_T)-1 - d) == NULL);
        T_TRUE(rt_heap_alloc(ph, HEAP_ZERO_MEMORY, (SIZE_T)-1 - d) == NULL);
        T_TRUE(rt_heap_alloc(ph, 0, (UINT64_C(1) << 63) + d) == NULL);
        T_TRUE(rt_heap_alloc(ph, 0, (UINT64_C(1) << 63) - 1 - d) == NULL);
    }
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);                /* each failed HeapAlloc sets it */
    /* Sizes the guest address space cannot hold are refused without asking the host: the largest
     * that could fit is limit - 16, and one byte more cannot. */
    T_TRUE(rt_heap_alloc(ph, 0, limit - 15) == NULL);
    T_TRUE(rt_heap_alloc(ph, 0, limit) == NULL);
    T_TRUE(rt_heap_alloc(ph, 0, limit + 1) == NULL);
    T_TRUE(rt_heap_alloc(ph, 0, (UINT64_C(1) << 47)) == NULL);
    T_TRUE(rt_heap_alloc(ph, 0, (UINT64_C(1) << 46)) == NULL);
    /* 4 TiB and more is refused without asking the host (see too_big in heap.c); just under it is the host's
     * to decide (it hands out address space lazily, so it may grant it), but it must never stop the process. */
    T_TRUE(rt_heap_alloc(ph, 0, UINT64_C(1) << 42) == NULL);
    T_TRUE(rt_heap_alloc(ph, HEAP_ZERO_MEMORY, (UINT64_C(1) << 42) + 1) == NULL);
    T_TRUE(rt_heap_alloc(ph, 0, limit - 16) == NULL);                 /* the largest that "fits" the address space */
    uint8_t *just_under = rt_heap_alloc(ph, 0, (UINT64_C(1) << 42) - 1);
    if (just_under) {
        T_EQ(rt_heap_size(ph, 0, just_under), (UINT64_C(1) << 42) - 1);
        T_EQ(rt_heap_free(ph, 0, just_under), TRUE);
    }
    /* Whether or not the host granted just_under, the code is still ERROR_NOT_ENOUGH_MEMORY: the
     * failures above set it, and a success leaves it alone. */
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_TRUE(T_LOGGED(RT_CH_VM, "returning NULL"));
    /* Still healthy. */
    void *p = rt_heap_alloc(ph, 0, 32);
    T_REQUIRE(p != NULL);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}
