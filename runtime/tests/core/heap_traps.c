/* Pointers that are not live blocks of the heap they are handed to, headers that were
 * overwritten, and the guest address limit.  Windows turns these into heap corruption, or into
 * a failure code that callers do not check; the runtime stops and names the pointer
 * (docs/RUNTIME.md sections 5.4 and 9.2, rt/heap.h).  Each statement runs in a forked child
 * (T_TRAPS), so the blocks it damages are untouched in the test itself.
 */
#include <malloc/malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/heap.h"
#include "rt/machine.h"

static RtHeapBlock *hdr(const void *p) { return (RtHeapBlock *)p - 1; }

TEST(core, heap_free_of_non_blocks_is_fatal)
{
    HANDLE ph = rt_heap_process();
    uint8_t *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);

    int on_stack[16] __attribute__((aligned(16)));
    void *host = malloc(100);
    T_REQUIRE(host != NULL);
    static uint8_t in_data[64] __attribute__((aligned(16)));

    T_TRAPS(rt_heap_free(ph, 0, on_stack), "heap: HeapFree:");
    T_TRAPS(rt_heap_free(ph, 0, on_stack), "is not a block of heap");
    T_TRAPS(rt_heap_free(ph, 0, host), "is not a block of heap");                 /* plain malloc memory */
    T_TRAPS(rt_heap_free(ph, 0, in_data), "is not a block of heap");              /* the host's data segment */
    T_TRAPS(rt_heap_free(ph, 0, p + 1), "16-byte aligned");                       /* misaligned */
    T_TRAPS(rt_heap_free(ph, 0, p + 8), "16-byte aligned");
    T_TRAPS(rt_heap_free(ph, 0, p + 15), "16-byte aligned");
    T_TRAPS(rt_heap_free(ph, 0, p + 16), "is not a block of heap");               /* aligned, inside the block */
    T_TRAPS(rt_heap_free(ph, 0, p + 32), "is not a block of heap");
    T_TRAPS(rt_heap_free(ph, 0, (void *)0x10), "outside the guest address range");
    T_TRAPS(rt_heap_free(ph, 0, (void *)0x10000), "outside the guest address range");   /* min_address, header would be below it */
    T_TRAPS(rt_heap_free(ph, 0, (void *)(rt_machine()->max_address + 1)), "outside the guest address range");
    T_TRAPS(rt_heap_free(ph, 0, (void *)UINT64_C(0xFFFFFFFFFFFFFFF0)), "outside the guest address range");
    T_TRAPS(rt_heap_free(ph, 0, (void *)UINT64_C(0x8000000000000000)), "outside the guest address range");
    /* The report names the pointer. */
    char want[64];
    snprintf(want, sizeof want, "%p is not a block of heap", (void *)on_stack);
    T_TRAPS(rt_heap_free(ph, 0, on_stack), want);

    /* None of that touched the real block. */
    T_EQ(rt_heap_size(ph, 0, p), 100);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
    free(host);
}

TEST(core, heap_double_free_is_fatal)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    static const uint64_t sizes[] = {0, 1, 16, 100, 1000, 5000, 100000, 3u << 20};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        for (unsigned k = 0; k < 2; k++) {
            HANDLE x = k ? h : ph;
            void *p = rt_heap_alloc(x, 0, sizes[i]);
            T_REQUIRE(p != NULL);
            T_EQ(rt_heap_free(x, 0, p), TRUE);
            T_TRAPS(rt_heap_free(x, 0, p), "already freed");
            T_TRAPS(rt_heap_realloc(x, 0, p, 10), "already freed");               /* use after free */
            T_TRAPS(rt_heap_realloc(x, HEAP_ZERO_MEMORY, p, 1u << 20), "already freed");
        }
    }
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_block_of_another_heap_is_fatal)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    void *a = rt_heap_alloc(ph, 0, 64);
    void *b = rt_heap_alloc(h, 0, 64);
    T_REQUIRE(a != NULL && b != NULL);
    /* Freed to, or reallocated in, the wrong heap: the report says which heap the block is in. */
    T_TRAPS(rt_heap_free(h, 0, a), "it is a block of heap");
    T_TRAPS(rt_heap_free(ph, 0, b), "it is a block of heap");
    T_TRAPS(rt_heap_realloc(h, 0, a, 100), "it is a block of heap");
    T_TRAPS(rt_heap_realloc(ph, HEAP_ZERO_MEMORY, b, 1u << 20), "it is a block of heap");
    char want[100];
    snprintf(want, sizeof want, "heap %u: it is a block of heap %u", hdr(a)->heap_id, hdr(b)->heap_id);
    T_TRAPS(rt_heap_free(ph, 0, b), want);
    /* rt_gfree is the process heap's free: another heap's block is refused there too. */
    T_TRAPS(rt_gfree(b), "heap: rt_gfree:");
    T_TRAPS(rt_gfree(b), "it is a block of heap");
    /* Both blocks are still good. */
    T_EQ(rt_heap_size(ph, 0, a), 64);
    T_EQ(rt_heap_size(h, 0, b), 64);

    /* A block of a destroyed heap: its memory is gone, so only the pointer can be named. */
    T_EQ(rt_heap_destroy(h), TRUE);
    T_TRAPS(rt_heap_free(ph, 0, b), "nor of any live heap");
    T_TRAPS(rt_gfree(b), "nor of any live heap");
    T_TRAPS(rt_heap_realloc(ph, 0, b, 5), "nor of any live heap");
    T_EQ(rt_heap_free(ph, 0, a), TRUE);
}

TEST(core, heap_overwritten_header_is_fatal)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    uint8_t *p = rt_heap_alloc(h, 0, 100);
    T_REQUIRE(p != NULL);
    uint32_t id = hdr(p)->heap_id;
    char want[100];

    /* A wrong magic: the block is "freed or overwritten", and the report shows the value found. */
    hdr(p)->magic = 0x12345678;
    T_TRAPS(rt_heap_free(h, 0, p), "block header with magic 0x12345678");
    T_TRAPS(rt_heap_realloc(h, 0, p, 10), "block header with magic 0x12345678");
    T_EQ(rt_heap_size(h, 0, p), (SIZE_T)-1);                       /* HeapSize: the documented failure return */
    hdr(p)->magic = 0;                                             /* what a freed block looks like */
    T_TRAPS(rt_heap_free(h, 0, p), "block header with magic 0, not 0x6b6c4248");
    T_TRAPS(rt_heap_free(h, 0, p), "double free");
    T_EQ(rt_heap_size(h, 0, p), (SIZE_T)-1);
    hdr(p)->magic = RT_HEAP_BLOCK_MAGIC + 1;
    T_TRAPS(rt_heap_free(h, 0, p), "block header with magic 0x6b6c4249");
    hdr(p)->magic = RT_HEAP_BLOCK_MAGIC;
    T_EQ(rt_heap_size(h, 0, p), 100);

    /* A wrong heap id, though the block is in this heap's zone. */
    hdr(p)->heap_id = id + 1;
    snprintf(want, sizeof want, "block header names heap id %u", id + 1);
    T_TRAPS(rt_heap_free(h, 0, p), want);
    T_TRAPS(rt_heap_realloc(h, 0, p, 200), want);
    T_EQ(rt_heap_size(h, 0, p), (SIZE_T)-1);
    hdr(p)->heap_id = 0;
    T_TRAPS(rt_heap_free(h, 0, p), "names heap id 0");
    hdr(p)->heap_id = hdr(rt_galloc(1))->heap_id;                  /* the process heap's id */
    T_TRAPS(rt_heap_free(h, 0, p), "names heap id");
    T_TRAPS(rt_heap_free(ph, 0, p), "it is a block of heap");      /* in the wrong zone and with the other heap's id */
    hdr(p)->heap_id = id;

    /* Intact again. */
    T_EQ(rt_heap_size(h, 0, p), 100);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_realloc_of_non_blocks_is_fatal)
{
    HANDLE ph = rt_heap_process();
    uint8_t *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    int on_stack[16] __attribute__((aligned(16)));
    /* realloc(NULL, n) is a C idiom, not a HeapReAlloc one; Windows' answer is not known here. */
    T_TRAPS(rt_heap_realloc(ph, 0, NULL, 10), "heap: HeapReAlloc: NULL block");
    T_TRAPS(rt_heap_realloc(ph, HEAP_ZERO_MEMORY, NULL, 0), "NULL block");
    T_TRAPS(rt_heap_realloc(ph, 0, on_stack, 10), "is not a block of heap");
    T_TRAPS(rt_heap_realloc(ph, 0, p + 1, 10), "16-byte aligned");
    T_TRAPS(rt_heap_realloc(ph, 0, p + 16, 10), "is not a block of heap");
    T_TRAPS(rt_heap_realloc(ph, 0, (void *)8, 10), "16-byte aligned");
    T_TRAPS(rt_heap_realloc(ph, 0, (void *)0x10, 10), "outside the guest address range");
    /* The bad pointer is reported even when the request itself would fail anyway. */
    T_TRAPS(rt_heap_realloc(ph, 0, on_stack, (SIZE_T)-1), "is not a block of heap");
    T_EQ(rt_heap_size(ph, 0, p), 100);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

TEST(core, heap_gfree_of_non_blocks_is_fatal)
{
    int on_stack[16] __attribute__((aligned(16)));
    void *p = rt_galloc(40);
    T_REQUIRE(p != NULL);
    T_TRAPS(rt_gfree(on_stack), "heap: rt_gfree: ");
    T_TRAPS(rt_gfree((char *)p + 1), "16-byte aligned");
    T_TRAPS(rt_gfree((void *)1), "16-byte aligned");
    T_TRAPS(rt_gfree((void *)0x10), "outside the guest address range");
    rt_gfree(p);
    T_TRAPS(rt_gfree(p), "already freed");
}

/* ---- the guest address limit -------------------------------------------------------------------- */

/* The tests below lower the limit the runtime believes in, to make the host's allocator look as if it
 * had returned an address above 8 TB, which it never does in practice.  The machine description is
 * ordinary writable memory; a test may patch it, in the child process or around one call. */
static void set_limit(uint64_t v) { ((RtMachine *)rt_machine())->max_address = v; }

/* Where does the zone put block `from` when it is reallocated to `to` bytes: +1 above the old
 * block, -1 below it, 0 in the same place (or the call failed)?  Found out in a forked child, which
 * starts from a copy of this process, so the same calls made here afterwards (and in a T_TRAPS child)
 * do the same.  The zone's layout changes from run to run, so a test cannot assume a direction. */
static int moves(uint64_t from, uint64_t to) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        void *p = rt_heap_alloc(rt_heap_process(), 0, from);
        void *q = p ? rt_heap_realloc(rt_heap_process(), 0, p, to) : NULL;
        _exit(!q ? 3 : (uint64_t)q > (uint64_t)p ? 1 : (uint64_t)q < (uint64_t)p ? 2 : 0);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) { }
    if (!WIFEXITED(st)) return 0;
    return WEXITSTATUS(st) == 1 ? 1 : WEXITSTATUS(st) == 2 ? -1 : 0;
}

/* Make every block but the old one invalid for the check: lower the limit to the old block's
 * address and raise the minimum to the old block's header.  The old block stays valid (its header
 * is at the minimum, its address at the limit); wherever the zone puts the new one, above or below,
 * it is outside.  An earlier version squeezed only the side that moves() predicted; the zone did
 * not always repeat the forked child's choice, and the test failed in about one run in four. */
static void squeeze(const void *old_block) {
    RtMachine *mc = (RtMachine *)rt_machine();
    mc->max_address = (uint64_t)(uintptr_t)old_block;
    mc->min_address = (uint64_t)(uintptr_t)old_block - sizeof(RtHeapBlock);
}

TEST(core, heap_moved_block_outside_the_address_range_is_fatal)
{
    /* A reallocation that moves the block puts it somewhere the zone chooses; that address is checked
     * like any other, growth and shrinking alike (a shrink into a smaller size class moves too). */
    HANDLE ph = rt_heap_process();
    const uint64_t real_max = rt_machine()->max_address, real_min = rt_machine()->min_address;
    static const struct { uint64_t from, to; } cases[] = {{1000, 70000}, {1000, 1u << 20}, {100, 5000}, {8000000, 100}, {8000000, 16}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int dir = moves(cases[i].from, cases[i].to);
        T_REQUIRE(dir != 0);                                   /* these sizes are in different size classes: the zone moves them */
        uint8_t *p = rt_heap_alloc(ph, 0, cases[i].from);
        T_REQUIRE(p != NULL);
        T_TRAPS((squeeze(p), rt_heap_realloc(ph, 0, p, cases[i].to)), "not entirely inside the guest address range");
        T_TRAPS((squeeze(p), rt_heap_realloc(ph, HEAP_ZERO_MEMORY, p, cases[i].to)), "heap: HeapReAlloc:");
        /* The block is as it was, and with the range intact the same call works. */
        T_EQ(rt_heap_size(ph, 0, p), cases[i].from);
        T_EQ(rt_machine()->max_address, real_max);
        T_EQ(rt_machine()->min_address, real_min);
        uint8_t *q = rt_heap_realloc(ph, 0, p, cases[i].to);
        T_REQUIRE(q != NULL);
        T_EQ(rt_heap_size(ph, 0, q), cases[i].to);
        T_EQ(rt_heap_free(ph, 0, q), TRUE);
    }
    /* With HEAP_REALLOC_IN_PLACE_ONLY a shrink does not move, so the squeeze does not matter. */
    uint8_t *p = rt_heap_alloc(ph, 0, 8000000);
    T_REQUIRE(p != NULL);
    T_TRUE(rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, 100) == p);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);
}

TEST(core, heap_block_above_the_address_limit_is_fatal)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    const uint64_t real = rt_machine()->max_address;

    /* A host zone that returned a block above the limit: here, a limit below every zone address. */
    T_TRAPS((set_limit(0x10000000), rt_heap_alloc(ph, 0, 100)), "not entirely inside the guest address range 0x10000..0x10000000");
    T_TRAPS((set_limit(0x10000000), rt_heap_alloc(h, HEAP_ZERO_MEMORY, 100)), "docs/RUNTIME.md 1.2, 5.4");
    T_TRAPS((set_limit(0x10000000), rt_galloc(100)), "heap: rt_galloc:");
    T_TRAPS((set_limit(0x10000000), rt_heap_alloc(ph, 0, 0)), "entirely inside the guest address range");           /* even a zero-size block */
    T_TRAPS((set_limit(0x10000000), rt_heap_alloc(ph, 0, 5000000)), "entirely inside the guest address range");     /* and a large one */

    /* Exact fit, and one byte over, at the end of a block.  Take a block with spare room in its zone
     * slot, so that it can be grown in place into the room by one byte at a time. */
    uint8_t *p = rt_heap_alloc(ph, 0, 100);
    T_REQUIRE(p != NULL);
    uint64_t room = malloc_size(hdr(p)) - sizeof(RtHeapBlock);
    T_REQUIRE(room >= 102);
    uint64_t base = (uint64_t)(uintptr_t)hdr(p);
    uint64_t last_of_101 = base + 16 + 101 - 1;                    /* the last byte a size-101 block uses */
    set_limit(last_of_101);                                        /* it fits to the byte... */
    uint8_t *q = rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, 101);
    set_limit(real);
    T_TRUE(q == p);
    T_EQ(rt_heap_size(ph, 0, q), 101);
    /* ...and one byte less room is one byte too many. */
    T_TRAPS((set_limit(last_of_101), rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY, p, 102)),
            "not entirely inside the guest address range");
    T_TRAPS((set_limit(last_of_101), rt_heap_realloc(ph, HEAP_REALLOC_IN_PLACE_ONLY | HEAP_ZERO_MEMORY, p, room)), "entirely inside the guest address range");
    set_limit(real);
    T_EQ(rt_heap_size(ph, 0, p), 101);
    T_EQ(rt_heap_free(ph, 0, p), TRUE);

    /* The last byte of the limit itself: a size so large that the block cannot end at or below it fails
     * without asking the host, as NULL (heap_alloc_failure_boundaries); the limit is a limit on blocks the
     * zone made, and it never makes a smaller block fail. */
    set_limit(real);
    void *ok = rt_heap_alloc(ph, 0, 100);
    T_TRUE(ok != NULL);
    T_EQ(rt_heap_free(ph, 0, ok), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
}
