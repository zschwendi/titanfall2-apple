/* The process heap and HeapCreate heaps: the Windows rules that hold for every heap
 * (docs/RUNTIME.md section 5.4, rt/heap.h; the Microsoft documentation of HeapAlloc,
 * HeapReAlloc, HeapFree, HeapSize).  The rest of the unit's tests: heap_create.c (creating
 * and destroying heaps, flags, fixed-size heaps, failure), heap_traps.c (foreign pointers),
 * heap_threads.c (concurrency), heap_stress.c (a random workload against a model).
 *
 * The core calls are made directly, as C: the core has no shim of its own to call through the
 * guest ABI, and core tests use no product shim.
 */
#include <malloc/malloc.h>
#include <stdlib.h>

#include "rt/test.h"
#include "rt/heap.h"
#include "rt/machine.h"

static RtHeapBlock *hdr(const void *p) { return (RtHeapBlock *)p - 1; }

/* Bytes of the zone's block behind a heap block, header included: how much room it really has. */
static uint64_t zone_room(const void *p) { return malloc_size(hdr(p)); }

static void fill(void *p, uint64_t n, uint8_t seed) {
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + i * 7);
}

static bool check_fill(const void *p, uint64_t n, uint8_t seed) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i] != (uint8_t)(seed + i * 7)) return false;
    return true;
}

static bool all_zero(const void *p, uint64_t n) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

/* ---- the process heap ------------------------------------------------------------------------ */

TEST(core, heap_process)
{
    HANDLE ph = rt_heap_process();
    T_REQUIRE(ph != NULL);
    T_TRUE(rt_heap_process() == ph);                            /* the same handle every time */
    T_EQ(*(uint32_t *)ph, RT_HEAP_MAGIC);                       /* a handle points at a header with the magic */
    T_EQ((uint64_t)ph & 15, 0);
    /* The handle is a guest address: inside what GetSystemInfo says, so below 8 TB. */
    T_TRUE((uint64_t)ph >= rt_machine()->min_address && (uint64_t)ph <= rt_machine()->max_address);

    /* The process block holds it, where guest code that reads it directly finds it
     * (rt/win/teb.h RT_PEB_PROCESS_HEAP).  The skeleton's core/process_block test still says
     * this field is 0: that assertion is stale now that this unit exists. */
    uint64_t peb = LD64(T_CTX()->gs_base + RT_TEB_PEB);
    T_EQ(LD64(peb + RT_PEB_PROCESS_HEAP), (uint64_t)ph);
    T_EQ(LD64(rt_process_block() + RT_PEB_PROCESS_HEAP), (uint64_t)ph);

    /* It works. */
    void *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    T_EQ(rt_heap_size(ph, 0, p), 100);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

TEST(core, heap_process_cannot_be_destroyed)
{
    HANDLE ph = rt_heap_process();
    rt_thread_set_last_error(0);
    T_EQ(rt_heap_destroy(ph), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    /* ...and it is still a working heap, with its block intact. */
    void *p = rt_heap_alloc(ph, HEAP_ZERO_MEMORY, 64);
    T_REQUIRE(p != NULL);
    T_TRUE(all_zero(p, 64));
    T_EQ(rt_heap_destroy(ph), FALSE);                           /* again: the same answer, not a double destroy */
    T_EQ(rt_heap_size(ph, 0, p), 64);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

/* ---- the block header and sizes ---------------------------------------------------------------- */

TEST(core, heap_block_layout)
{
    /* Every size around the zone's own quanta (16, 512, 4096, 16 KB pages), the documented
     * 0xFF000 limit of fixed-size heaps (ours, rt/heap.h; this heap grows, so none of it matters)
     * and a megabyte: exact size, alignment, the header, no overlap, all bytes usable. */
    static const uint64_t sizes[] = {
        0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 47, 48, 49, 63, 64, 65, 100, 127, 128, 129, 255, 256, 257,
        511, 512, 513, 1000, 1023, 1024, 1025, 4095, 4096, 4097, 16383, 16384, 16385, 65535, 65536, 65537,
        0x7FFF7, 0x7FFF8, 0x7FFF9, 0xFEFFF, 0xFF000, 0xFF001, (1u << 20) - 1, 1u << 20, (1u << 20) + 1, 8u << 20
    };
    enum { N = sizeof sizes / sizeof sizes[0] };
    HANDLE ph = rt_heap_process();
    void *blk[N];
    uint32_t id = 0;
    for (unsigned i = 0; i < N; i++) {
        blk[i] = rt_heap_alloc(ph, 0, sizes[i]);
        T_REQUIRE(blk[i] != NULL);
        T_EQ((uint64_t)blk[i] & 15, 0);                         /* 16-byte aligned, as on x64 Windows */
        T_EQ(hdr(blk[i])->magic, RT_HEAP_BLOCK_MAGIC);
        T_EQ(hdr(blk[i])->size, sizes[i]);
        if (i == 0) id = hdr(blk[i])->heap_id; else T_EQ(hdr(blk[i])->heap_id, id);    /* one heap, one id */
        T_EQ(rt_heap_size(ph, 0, blk[i]), sizes[i]);            /* exactly what was asked, not what the zone rounded to */
        T_TRUE((uint64_t)blk[i] + sizes[i] <= rt_machine()->max_address);
        fill(blk[i], sizes[i], (uint8_t)i);
    }
    T_TRUE(id != 0);
    for (unsigned i = 0; i < N; i++) {
        T_TRUE(check_fill(blk[i], sizes[i], (uint8_t)i));       /* nobody wrote into anybody else's bytes */
        T_EQ(rt_heap_size(ph, 0, blk[i]), sizes[i]);
        for (unsigned j = 0; j < i; j++) T_TRUE(blk[i] != blk[j]);
    }
    for (unsigned i = 0; i < N; i++) T_EQ(rt_heap_free(ph, 0, blk[i]), TRUE);
}

TEST(core, heap_zero_size_blocks)
{
    /* A zero-size request is a valid, unique pointer that HeapSize reports as 0 and HeapFree
     * accepts (HeapAlloc docs). */
    HANDLE ph = rt_heap_process();
    void *blk[200];
    for (unsigned i = 0; i < 200; i++) {
        blk[i] = rt_heap_alloc(ph, i & 1 ? HEAP_ZERO_MEMORY : 0, 0);
        T_REQUIRE(blk[i] != NULL);
        T_EQ((uint64_t)blk[i] & 15, 0);
        T_EQ(rt_heap_size(ph, 0, blk[i]), 0);
        for (unsigned j = 0; j < i; j++) T_REQUIRE(blk[i] != blk[j]);
    }
    for (unsigned i = 0; i < 200; i++) T_EQ(rt_heap_free(ph, 0, blk[i]), TRUE);
}

TEST(core, heap_huge_sizes_are_exact)
{
    /* A SIZE_T is 64 bits: a size above 4 GiB must not be truncated anywhere.  Only the first and last
     * page are touched, so this costs address space and almost no memory. */
    HANDLE ph = rt_heap_process();
    const uint64_t n = (UINT64_C(5) << 30) + 3;
    uint8_t *p = rt_heap_alloc(ph, HEAP_ZERO_MEMORY, n);
    T_REQUIRE(p != NULL);
    T_EQ(rt_heap_size(ph, 0, p), n);
    T_EQ(hdr(p)->size, n);
    T_TRUE((uint64_t)p + n <= rt_machine()->max_address);
    T_EQ(p[0], 0);
    T_EQ(p[n - 1], 0);
    T_EQ(p[n / 2], 0);
    p[n - 1] = 0x5A;
    p[0] = 0xA5;
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

/* ---- HEAP_ZERO_MEMORY --------------------------------------------------------------------------- */

TEST(core, heap_zero_memory)
{
    /* The block is dirtied over its whole room, freed, and requested again: whatever the zone
     * hands back for the same size class, HEAP_ZERO_MEMORY makes exactly the requested bytes
     * zero.  Without the flag the contents are unspecified, so nothing is asserted. */
    static const uint64_t sizes[] = {1, 15, 16, 17, 100, 511, 512, 513, 4000, 4096, 70000, 1u << 20};
    HANDLE ph = rt_heap_process();
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        uint64_t n = sizes[s];
        for (unsigned round = 0; round < 4; round++) {
            uint8_t *d = rt_heap_alloc(ph, 0, n);
            T_REQUIRE(d != NULL);
            memset(d, 0xAB, zone_room(d) - sizeof(RtHeapBlock));          /* the whole usable room */
            T_EQ(rt_heap_free(ph, 0, d), TRUE);
            uint8_t *z = rt_heap_alloc(ph, HEAP_ZERO_MEMORY, n);
            T_REQUIRE(z != NULL);
            T_TRUE(all_zero(z, n));
            T_EQ(rt_heap_size(ph, 0, z), n);
            memset(z, 0xCD, zone_room(z) - sizeof(RtHeapBlock));
            T_EQ(rt_heap_free(ph, 0, z), TRUE);
        }
    }
}

/* ---- HeapFree and HeapSize --------------------------------------------------------------------- */

TEST(core, heap_free_rules)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    /* A NULL pointer is accepted and returns TRUE, in every heap, with the flag that is allowed. */
    T_EQ(rt_heap_free(ph, 0, NULL), TRUE);
    T_EQ(rt_heap_free(h, 0, NULL), TRUE);
    T_EQ(rt_heap_free(h, HEAP_NO_SERIALIZE, NULL), TRUE);

    /* Freed blocks: HeapSize no longer answers.  (The header's magic is cleared too, but the zone
     * reuses the first bytes of a freed block for its own links, so the test cannot read it; the
     * double-free tests in heap_traps.c are what show the block is refused.) */
    for (unsigned i = 0; i < 2; i++) {
        HANDLE x = i ? h : ph;
        void *p = rt_heap_alloc(x, 0, 200);
        T_REQUIRE(p != NULL);
        T_EQ(rt_heap_free(x, i ? HEAP_NO_SERIALIZE : 0, p), TRUE);
        T_EQ(rt_heap_size(x, 0, p), (SIZE_T)-1);
    }
    /* Memory returned to the heap is reused: a freed size comes back without growing forever. */
    for (unsigned i = 0; i < 100000; i++) {
        void *p = rt_heap_alloc(h, 0, 48);
        T_REQUIRE(p != NULL);
        T_EQ(rt_heap_free(h, 0, p), TRUE);
    }
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_size_rules)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    void *a = rt_heap_alloc(ph, 0, 123);
    void *b = rt_heap_alloc(h, 0, 456);
    T_REQUIRE(a != NULL && b != NULL);

    /* Exactly the requested size, with HEAP_NO_SERIALIZE too. */
    T_EQ(rt_heap_size(ph, 0, a), 123);
    T_EQ(rt_heap_size(ph, HEAP_NO_SERIALIZE, a), 123);
    T_EQ(rt_heap_size(h, 0, b), 456);

    /* The documented failure return, (SIZE_T)-1, for a pointer that is not a live block of that
     * heap, and no last error ("The function does not call SetLastError"). */
    rt_thread_set_last_error(0x5151);
    T_EQ(rt_heap_size(ph, 0, NULL), (SIZE_T)-1);
    T_EQ(rt_heap_size(ph, 0, b), (SIZE_T)-1);                    /* a live block, but of the other heap */
    T_EQ(rt_heap_size(h, 0, a), (SIZE_T)-1);
    T_EQ(rt_heap_size(ph, 0, (char *)a + 16), (SIZE_T)-1);       /* aligned, inside a block, not a block */
    T_EQ(rt_heap_size(ph, 0, (char *)a + 1), (SIZE_T)-1);        /* misaligned */
    T_EQ(rt_heap_size(ph, 0, (void *)0x10), (SIZE_T)-1);         /* below the guest range */
    T_EQ(rt_heap_size(ph, 0, (void *)(rt_machine()->max_address + 1)), (SIZE_T)-1);
    T_EQ(rt_heap_size(ph, 0, (void *)UINT64_MAX), (SIZE_T)-1);
    int on_stack[8];
    T_EQ(rt_heap_size(ph, 0, on_stack), (SIZE_T)-1);
    void *host = malloc(64);
    T_EQ(rt_heap_size(ph, 0, host), (SIZE_T)-1);                 /* a plain malloc pointer */
    free(host);
    T_EQ(T_LAST_ERROR(), 0x5151);

    /* After HeapFree the block is gone. */
    T_EQ(rt_heap_free(ph, 0, a), TRUE);
    T_EQ(rt_heap_size(ph, 0, a), (SIZE_T)-1);
    T_EQ(T_LAST_ERROR(), 0x5151);
    T_EQ(rt_heap_size(h, 0, b), 456);                            /* the other heap is untouched */

    /* A block of a destroyed heap is not a block of any heap (its memory is gone: only the
     * pointer is looked at, never what it points to). */
    T_EQ(rt_heap_destroy(h), TRUE);
    T_EQ(rt_heap_size(ph, 0, b), (SIZE_T)-1);
}

/* ---- HeapReAlloc ----------------------------------------------------------------------------- */

TEST(core, heap_realloc_keeps_contents)
{
    HANDLE ph = rt_heap_process();
    static const uint64_t steps[] = {1, 10, 16, 17, 100, 100, 1000, 20, 5000, 4096, 70000, 70000, 3, 1u << 20, 17, 0, 40, 4000000, 8};
    uint8_t *p = rt_heap_alloc(ph, 0, 0);
    T_REQUIRE(p != NULL);
    uint64_t have = 0;
    uint8_t seed = 1;
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        uint64_t want = steps[i];
        uint8_t *q = rt_heap_realloc(ph, 0, p, want);            /* may move or not */
        T_REQUIRE(q != NULL);
        T_EQ((uint64_t)q & 15, 0);
        T_EQ(rt_heap_size(ph, 0, q), want);
        T_EQ(hdr(q)->size, want);
        T_EQ(hdr(q)->magic, RT_HEAP_BLOCK_MAGIC);
        uint64_t keep = have < want ? have : want;               /* the common prefix survives */
        T_TRUE(check_fill(q, keep, seed));
        seed = (uint8_t)(seed + 13);
        fill(q, want, seed);
        have = want;
        p = q;
    }
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

TEST(core, heap_realloc_zero_memory_zeroes_exactly_the_growth)
{
    /* HEAP_ZERO_MEMORY on HeapReAlloc zeroes the bytes from the old requested size to the new
     * one, whatever is in the zone's slack and in a moved copy of it.  Each block's whole room
     * is dirtied first so the check can tell. */
    HANDLE ph = rt_heap_process();
    static const struct { uint64_t from, to; } cases[] = {
        {1, 2}, {1, 8}, {10, 11}, {10, 24}, {16, 17}, {50, 80}, {100, 101}, {100, 5000}, {4000, 4096}, {4096, 4097},
        {1000, 100000}, {70000, 70001}, {70000, 2000000}, {0, 1}, {0, 64}, {0, 100000}
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        for (unsigned mode = 0; mode < 2; mode++) {
            uint8_t *p = rt_heap_alloc(ph, 0, cases[c].from);
            T_REQUIRE(p != NULL);
            memset(p, 0xEE, zone_room(p) - sizeof(RtHeapBlock));
            fill(p, cases[c].from, 77);
            uint8_t *q = rt_heap_realloc(ph, mode ? HEAP_ZERO_MEMORY | HEAP_NO_SERIALIZE : HEAP_ZERO_MEMORY, p, cases[c].to);
            T_REQUIRE(q != NULL);
            T_EQ(rt_heap_size(ph, 0, q), cases[c].to);
            T_TRUE(check_fill(q, cases[c].from, 77));            /* the old bytes are kept... */
            T_TRUE(all_zero(q + cases[c].from, cases[c].to - cases[c].from));   /* ...and the new ones are zero */
            T_EQ(rt_heap_free(ph, 0, q), TRUE);
        }
    }

    /* Growth in place through the slack (the block's room is bigger than its size), several
     * times, with garbage written into the slack between the steps. */
    uint8_t *p = rt_heap_alloc(ph, 0, 33);
    T_REQUIRE(p != NULL);
    uint64_t room = zone_room(p) - sizeof(RtHeapBlock);
    T_REQUIRE(room > 33);
    for (uint64_t n = 34; n <= room; n++) {
        memset(p, 0xEE, room);
        uint8_t *q = rt_heap_realloc(ph, HEAP_ZERO_MEMORY | HEAP_REALLOC_IN_PLACE_ONLY, p, n - 1);   /* shrink: stays */
        T_TRUE(q == p);
        memset(p + n - 1, 0xEE, room - (n - 1));                 /* garbage above the size */
        q = rt_heap_realloc(ph, HEAP_ZERO_MEMORY | HEAP_REALLOC_IN_PLACE_ONLY, p, n);
        T_REQUIRE(q == p);
        T_EQ(p[n - 1], 0);                                        /* the one added byte is zero */
    }
    T_EQ(rt_heap_free(ph, 0, p), TRUE);

    /* Without the flag nothing is promised about the added bytes, but the old ones stay. */
    p = rt_heap_alloc(ph, 0, 40);
    T_REQUIRE(p != NULL);
    fill(p, 40, 9);
    p = rt_heap_realloc(ph, 0, p, 400000);
    T_REQUIRE(p != NULL);
    T_TRUE(check_fill(p, 40, 9));
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

TEST(core, heap_realloc_to_zero_and_from_zero)
{
    /* Reallocating to 0 does not free: the block stays a valid, freeable, size-0 block (the
     * same rule as HeapAlloc(0)).  UNSURE of Windows here; see the comment in heap.c. */
    HANDLE ph = rt_heap_process();
    uint8_t *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    fill(p, 100, 3);
    uint8_t *q = rt_heap_realloc(ph, 0, p, 0);
    T_REQUIRE(q != NULL);
    T_EQ((uint64_t)q & 15, 0);
    T_EQ(rt_heap_size(ph, 0, q), 0);
    T_EQ(hdr(q)->magic, RT_HEAP_BLOCK_MAGIC);
    /* ...and grows again, with the zeroed growth counted from 0. */
    memset(q, 0xEE, zone_room(q) - sizeof(RtHeapBlock));
    uint8_t *r = rt_heap_realloc(ph, HEAP_ZERO_MEMORY, q, 50);
    T_REQUIRE(r != NULL);
    T_TRUE(all_zero(r, 50));
    T_EQ(rt_heap_size(ph, 0, r), 50);
    T_EQ(rt_heap_free(ph, 0, r), TRUE);

    /* With HEAP_REALLOC_IN_PLACE_ONLY: shrinking to 0 always works and keeps the address. */
    p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, 0) == p);
    T_EQ(rt_heap_size(ph, 0, p), 0);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

TEST(core, heap_realloc_same_size)
{
    HANDLE ph = rt_heap_process();
    uint8_t *p = rt_heap_alloc(ph, 0, 77);
    T_REQUIRE(p != NULL);
    fill(p, 77, 5);
    uint8_t *q = rt_heap_realloc(ph, 0, p, 77);
    T_REQUIRE(q != NULL);
    T_EQ(rt_heap_size(ph, 0, q), 77);
    T_TRUE(check_fill(q, 77, 5));
    /* Same size with the in-place flag: nothing to move, so it succeeds at the same address. */
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, q, 77) == q);
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY | HEAP_ZERO_MEMORY, q, 77) == q);   /* nothing added, nothing zeroed */
    T_TRUE(check_fill(q, 77, 5));
    T_EQ(rt_heap_free(ph, 0, q), TRUE);
}

TEST(core, heap_realloc_in_place_only)
{
    HANDLE ph = rt_heap_process();
    uint8_t *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    fill(p, 100, 11);
    uint64_t room = zone_room(p) - sizeof(RtHeapBlock);

    /* Shrinking keeps the address and the contents, and the new size is exact. */
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, 60) == p);
    T_EQ(rt_heap_size(ph, 0, p), 60);
    T_TRUE(check_fill(p, 60, 11));
    /* Growing back inside the room the block already has keeps the address too. */
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, room) == p);
    T_EQ(rt_heap_size(ph, 0, p), room);
    T_TRUE(check_fill(p, 60, 11));

    /* Growing past the room fails: NULL, no last error, and the block is exactly as it was. */
    rt_thread_set_last_error(0x7171);
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, room + 1) == NULL);
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY | HEAP_ZERO_MEMORY, p, room + 4096) == NULL);
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, 1u << 20) == NULL);
    T_EQ(T_LAST_ERROR(), 0x7171);
    T_EQ(rt_heap_size(ph, 0, p), room);
    T_EQ(hdr(p)->magic, RT_HEAP_BLOCK_MAGIC);
    T_TRUE(check_fill(p, 60, 11));
    T_TRUE(T_LOGGED(RT_CH_VM, "HEAP_REALLOC_IN_PLACE_ONLY and the block cannot grow in place"));

    /* Without the flag the same request succeeds, by moving. */
    uint8_t *q = rt_heap_realloc(ph, 0, p, 1u << 20);
    T_REQUIRE(q != NULL);
    T_TRUE(check_fill(q, 60, 11));
    T_EQ(rt_heap_size(ph, 0, q), 1u << 20);
    T_EQ(rt_heap_free(ph, 0, q), TRUE);
}

TEST(core, heap_realloc_failure_leaves_block_alone)
{
    HANDLE ph = rt_heap_process();
    uint8_t *p = rt_heap_alloc(ph, 0, 5000);
    T_REQUIRE(p != NULL);
    fill(p, 5000, 21);
    rt_thread_set_last_error(0x4242);
    const uint64_t huge[] = {(uint64_t)-1, (uint64_t)-16, UINT64_C(1) << 63, UINT64_C(1) << 48, UINT64_C(1) << 46,
                             rt_machine()->max_address, rt_machine()->max_address - 15, UINT64_C(1) << 42, (UINT64_C(1) << 42) + 7};
    for (unsigned i = 0; i < sizeof huge / sizeof huge[0]; i++) {
        T_TRUE(rt_heap_realloc(ph, 0, p, huge[i]) == NULL);
        T_TRUE(rt_heap_realloc(ph, HEAP_ZERO_MEMORY, p, huge[i]) == NULL);
        T_EQ(rt_heap_size(ph, 0, p), 5000);                       /* "the original memory is not freed" */
        T_TRUE(check_fill(p, 5000, 21));
    }
    T_EQ(T_LAST_ERROR(), 0x4242);                                 /* failure does not set the last error */
    T_TRUE(T_LOGGED(RT_CH_VM, "larger than any block the guest address space can hold"));
    T_EQ(rt_heap_free(ph, 0, p), TRUE);                           /* and it is still freeable */
}

/* ---- last error: success leaves it alone; a failed HeapAlloc sets ERROR_NOT_ENOUGH_MEMORY ------ */

TEST(core, heap_last_error_success_untouched)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    rt_thread_set_last_error(0x1234ABCD);
    void *p = rt_heap_alloc(ph, 0, 10);
    void *q = rt_heap_alloc(h, HEAP_ZERO_MEMORY, 0);
    T_REQUIRE(p != NULL && q != NULL);
    p = rt_heap_realloc(ph, 0, p, 1000);
    T_REQUIRE(p != NULL);
    T_EQ(rt_heap_size(ph, 0, p), 1000);
    T_EQ(rt_heap_size(h, 0, q), 0);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
    T_EQ(rt_heap_free(h, 0, q), TRUE);
    T_EQ(rt_heap_free(h, 0, NULL), TRUE);
    void *g = rt_galloc(100);
    rt_gfree(g);
    rt_gfree(NULL);
    T_EQ(rt_heap_destroy(h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234ABCD);

    /* Failures: a failed HeapAlloc sets ERROR_NOT_ENOUGH_MEMORY.  Windows: Wine
     * kernel32/tests/heap.c test_HeapCreate expects that code for an allocation that cannot fit,
     * in three places, none marked broken or todo_wine; the Microsoft page says HeapAlloc does
     * not call SetLastError, and the test contradicts the page. */
    rt_thread_set_last_error(0x1234ABCD);
    T_TRUE(rt_heap_alloc(ph, 0, (SIZE_T)-1) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    rt_thread_set_last_error(0x1234ABCD);
    T_TRUE(rt_heap_alloc(ph, HEAP_ZERO_MEMORY, UINT64_C(1) << 62) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
}

/* ---- rt_galloc / rt_gfree ---------------------------------------------------------------------- */

TEST(core, heap_galloc)
{
    HANDLE ph = rt_heap_process();
    void *probe = rt_heap_alloc(ph, 0, 1);
    T_REQUIRE(probe != NULL);
    uint32_t process_id = hdr(probe)->heap_id;
    static const uint64_t sizes[] = {0, 1, 8, 15, 16, 17, 64, 100, 4096, 70000, 1u << 20};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        /* Dirty the room first so that zero-filled means the call zeroed it. */
        void *d = rt_galloc(sizes[i]);
        T_REQUIRE(d != NULL);
        memset(d, 0xEE, zone_room(d) - sizeof(RtHeapBlock));
        rt_gfree(d);

        uint8_t *p = rt_galloc(sizes[i]);
        T_REQUIRE(p != NULL);
        T_EQ((uint64_t)p & 15, 0);
        T_TRUE(all_zero(p, sizes[i]));                            /* "rt_galloc zeroes" */
        /* It is a block of the process heap: HeapSize sees it, and the guest could HeapFree it. */
        T_EQ(rt_heap_size(ph, 0, p), sizes[i]);
        T_EQ(hdr(p)->heap_id, process_id);
        if (i & 1) rt_gfree(p);
        else T_EQ(rt_heap_free(ph, 0, p), TRUE);
    }
    /* ...and the other way: memory from HeapAlloc on the process heap goes back by rt_gfree. */
    void *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    rt_gfree(p);
    T_EQ(rt_heap_size(ph, 0, p), (SIZE_T)-1);
    rt_gfree(NULL);                                               /* accepted, like HeapFree(NULL) */
    T_EQ(rt_heap_free(ph, 0, probe), TRUE);
}

TEST(core, heap_galloc_unique_and_usable)
{
    enum { N = 500 };
    uint8_t *blk[N];
    for (unsigned i = 0; i < N; i++) {
        blk[i] = rt_galloc(i);
        T_REQUIRE(blk[i] != NULL);
        fill(blk[i], i, (uint8_t)i);
    }
    for (unsigned i = 0; i < N; i++) {
        T_TRUE(check_fill(blk[i], i, (uint8_t)i));
        T_EQ(rt_heap_size(rt_heap_process(), 0, blk[i]), i);
        rt_gfree(blk[i]);
    }
}

TEST(core, heap_validate)
{
    /* rt_heap_validate: the test HeapFree applies, as an answer instead of a fatal report
     * (rt/heap.h; HeapValidate: zero for an invalid heap or block, no last error). */
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    uint8_t *p = rt_heap_alloc(h, 0, 100);
    uint8_t *z = rt_heap_alloc(h, 0, 0);
    uint8_t *q = rt_heap_alloc(ph, 0, 64);
    T_REQUIRE(p && z && q);
    rt_set_last_error(T_CTX(), 0xABCD);

    /* A live block of the heap, of any size, with or without HEAP_NO_SERIALIZE. */
    T_EQ(rt_heap_validate(h, 0, p), TRUE);
    T_EQ(rt_heap_validate(h, HEAP_NO_SERIALIZE, p), TRUE);
    T_EQ(rt_heap_validate(h, 0, z), TRUE);
    T_EQ(rt_heap_validate(ph, 0, q), TRUE);
    /* No block: the heap itself. */
    T_EQ(rt_heap_validate(h, 0, NULL), TRUE);
    T_EQ(rt_heap_validate(ph, 0, NULL), TRUE);

    /* Not a live block of THIS heap: another heap's block, an interior pointer, a misaligned
     * one, host memory that no heap made, an address outside guest memory, a freed block. */
    int on_stack[8] = { 0 };
    void *host = malloc(64);
    T_REQUIRE(host != NULL);
    T_EQ(rt_heap_validate(h, 0, q), FALSE);
    T_EQ(rt_heap_validate(ph, 0, p), FALSE);
    T_EQ(rt_heap_validate(h, 0, p + 16), FALSE);
    T_EQ(rt_heap_validate(h, 0, p + 1), FALSE);
    T_EQ(rt_heap_validate(h, 0, on_stack), FALSE);
    T_EQ(rt_heap_validate(h, 0, host), FALSE);
    T_EQ(rt_heap_validate(h, 0, (void *)(uintptr_t)16), FALSE);
    T_EQ(rt_heap_validate(h, 0, (void *)(uintptr_t)UINT64_C(0x7FFFFFFFFFF0)), FALSE);
    free(host);
    T_TRUE(rt_heap_free(h, 0, z));
    T_EQ(rt_heap_validate(h, 0, z), FALSE);
    /* A block whose header was overwritten is found when that block is validated; the
     * whole-heap form walks nothing and does not see it. */
    uint32_t saved = hdr(p)->magic;
    hdr(p)->magic = 0x12345678;
    T_EQ(rt_heap_validate(h, 0, p), FALSE);
    T_EQ(rt_heap_validate(h, 0, NULL), TRUE);
    hdr(p)->magic = saved;
    uint32_t id = hdr(p)->heap_id;
    hdr(p)->heap_id = id + 1000;
    T_EQ(rt_heap_validate(h, 0, p), FALSE);
    hdr(p)->heap_id = id;
    T_EQ(rt_heap_validate(h, 0, p), TRUE);

    /* A heap handle that is not a live heap is an answer too, with or without a block:
     * NULL, something that is not a heap header, the middle of a header, a destroyed heap. */
    T_EQ(rt_heap_validate(NULL, 0, NULL), FALSE);
    T_EQ(rt_heap_validate(NULL, 0, p), FALSE);
    T_EQ(rt_heap_validate((HANDLE)on_stack, 0, NULL), FALSE);
    T_EQ(rt_heap_validate((HANDLE)p, 0, p), FALSE);
    T_EQ(rt_heap_validate((HANDLE)((char *)h + 8), 0, NULL), FALSE);
    T_EQ(rt_heap_validate((HANDLE)((char *)h + 8), 0, p), FALSE);
    T_TRUE(rt_heap_destroy(h));
    T_EQ(rt_heap_validate(h, 0, NULL), FALSE);
    T_EQ(rt_heap_validate(h, 0, p), FALSE);

    /* It never sets last error. */
    T_EQ(T_LAST_ERROR(), 0xABCD);
    T_TRUE(rt_heap_free(ph, 0, q));

    /* A flag bit it does not take is fatal, as everywhere in this unit. */
    T_TRAPS(rt_heap_validate(ph, HEAP_ZERO_MEMORY, NULL), "heap: HeapValidate: flags 0x8 contain 0x8");
}
