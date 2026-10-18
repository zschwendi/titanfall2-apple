#include <string.h>
#include "rt/test.h"
#include "rt/heap.h"

/* How many of the n bytes at p are not v. */
static unsigned bytes_not(const uint8_t *p, unsigned n, uint8_t v)
{
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++) if (p[i] != v) bad++;
    return bad;
}

TEST(kernel32, HeapReAlloc)
{
    HANDLE h = CALL(kernel32, GetProcessHeap);
    HANDLE fh;
    uint8_t *p, *q;
    int local[4] __attribute__((aligned(16)));

    T_CASE("grows a block and keeps its contents");
    p = CALL(kernel32, HeapAlloc, h, 0, 16);
    T_TRUE(p != NULL);
    memset(p, 0x5C, 16);
    q = CALL(kernel32, HeapReAlloc, h, 0, p, 4096);
    T_TRUE(q != NULL);
    T_EQ(rt_heap_size(h, 0, q), 4096);
    T_EQ(bytes_not(q, 16, 0x5C), 0);
    /* Refused, with the block untouched: a bit rt/heap.h does not know ... */
    T_TRAPS(CALL(kernel32, HeapReAlloc, h, 0x2, q, 64), "flag bits that are not in rt/heap.h");
    /* ... a bit it knows but HeapReAlloc does not take (the core stops on it) ... */
    T_TRAPS(CALL(kernel32, HeapReAlloc, h, HEAP_CREATE_ENABLE_EXECUTE, q, 64), "which this function does not take");
    /* ... and a pointer that is not a block of this heap. */
    T_TRAPS(CALL(kernel32, HeapReAlloc, h, 0, local, 64), "is not a block of heap");
    T_EQ(bytes_not(q, 16, 0x5C), 0);
    T_EQ(CALL(kernel32, HeapFree, h, 0, q), TRUE);

    /* In a fixed-size heap (made with rt_heap_create, as the HeapAlloc test says), a block of 0x40000
     * grows to 0x80000: Windows, 64-bit, does that (Wine kernel32/tests/heap.c test_HeapCreate:
     * HeapReAlloc(heap, HEAP_ZERO_MEMORY, ptr, 0x80000) succeeds, HeapSize is 0x80000, no broken).
     * The old contents are kept, every byte of the new size is usable, and last error stays. */
    fh = rt_heap_create(0, 0x200000, 0x200000);
    T_TRUE(fh != NULL);
    p = CALL(kernel32, HeapAlloc, fh, 0, 0x40000);
    T_TRUE(p != NULL);
    memset(p, 0x2F, 0x40000);
    CALL(kernel32, SetLastError, 0x1234);
    q = CALL(kernel32, HeapReAlloc, fh, 0, p, 0x80000);
    T_TRUE(q != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_size(fh, 0, q), 0x80000);
    T_EQ(bytes_not(q, 0x40000, 0x2F), 0);
    memset(q, 0x4D, 0x80000);
    T_EQ(bytes_not(q, 0x80000, 0x4D), 0);
    T_EQ(CALL(kernel32, HeapFree, fh, 0, q), TRUE);
    T_EQ(rt_heap_destroy(fh), TRUE);

    T_CASE("shrinks a block and keeps its contents");
    p = CALL(kernel32, HeapAlloc, h, 0, 4096);
    T_TRUE(p != NULL);
    memset(p, 0x3E, 4096);
    q = CALL(kernel32, HeapReAlloc, h, 0, p, 16);
    T_TRUE(q != NULL);
    T_EQ(rt_heap_size(h, 0, q), 16);
    T_EQ(bytes_not(q, 16, 0x3E), 0);
    T_EQ(CALL(kernel32, HeapFree, h, 0, q), TRUE);

    T_CASE("HEAP_ZERO_MEMORY zeroes the growth and nothing else");
    p = CALL(kernel32, HeapAlloc, h, 0, 16);
    T_TRUE(p != NULL);
    memset(p, 0x77, 16);
    q = CALL(kernel32, HeapReAlloc, h, HEAP_ZERO_MEMORY, p, 4096);
    T_TRUE(q != NULL);
    T_EQ(bytes_not(q, 16, 0x77), 0);             /* the old contents are kept ... */
    T_EQ(bytes_not(q + 16, 4096 - 16, 0), 0);    /* ... and the growth is zero */
    T_EQ(CALL(kernel32, HeapFree, h, 0, q), TRUE);

    T_CASE("HEAP_REALLOC_IN_PLACE_ONLY fails with NULL and leaves the block unchanged when it cannot stay put");
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    memset(p, 0x42, 64);
    q = CALL(kernel32, HeapReAlloc, h, HEAP_REALLOC_IN_PLACE_ONLY, p, (SIZE_T)1 << 20);
    T_TRUE(q == NULL);
    T_EQ(rt_heap_size(h, 0, p), 64);
    T_EQ(bytes_not(p, 64, 0x42), 0);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    T_CASE("a size too large for any block returns NULL and leaves the block unchanged");
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    memset(p, 0x24, 64);
    q = CALL(kernel32, HeapReAlloc, h, 0, p, (SIZE_T)1 << 42);
    T_TRUE(q == NULL);
    T_EQ(rt_heap_size(h, 0, p), 64);
    T_EQ(bytes_not(p, 64, 0x24), 0);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    T_CASE("does not change last error");
    p = CALL(kernel32, HeapAlloc, h, 0, 32);
    T_TRUE(p != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    q = CALL(kernel32, HeapReAlloc, h, 0, p, 64);
    T_TRUE(q != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    p = q;
    CALL(kernel32, SetLastError, 0x1234);
    q = CALL(kernel32, HeapReAlloc, h, 0, p, (SIZE_T)1 << 42);
    T_TRUE(q == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    /* A NULL block is not a stop.  Windows returns NULL and last error NO_ERROR (ERROR_SUCCESS):
     * Wine kernel32/tests/heap.c test_HeapCreate, HeapReAlloc(heap, 0, NULL, 1) returns NULL with
     * no todo, and its last error check is todo_wine (Windows does it; Wine did not yet).  Nothing
     * else changes: the live block keeps its size and contents, and the heap stays valid. */
    T_CASE("a NULL block returns NULL, sets last error to ERROR_SUCCESS and changes nothing");
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    memset(p, 0x6B, 64);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    q = CALL(kernel32, HeapReAlloc, h, 0, NULL, 1);
    T_TRUE(q == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_heap_size(h, 0, p), 64);
    T_EQ(bytes_not(p, 64, 0x6B), 0);
    T_EQ(rt_heap_validate(h, 0, NULL), TRUE);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    T_CASE("a size of 0 succeeds and returns a live block of size 0");
    p = CALL(kernel32, HeapAlloc, h, 0, 48);
    T_TRUE(p != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    q = CALL(kernel32, HeapReAlloc, h, 0, p, 0);
    T_TRUE(q != NULL);                                  /* not NULL: the call did not fail ... */
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_size(h, 0, q), 0);                     /* ... and the block is live, with size 0, */
    T_EQ(rt_heap_validate(h, 0, q), TRUE);
    p = CALL(kernel32, HeapReAlloc, h, HEAP_ZERO_MEMORY, q, 24);    /* can grow again, */
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 24);
    T_EQ(bytes_not(p, 24, 0), 0);
    q = CALL(kernel32, HeapReAlloc, h, HEAP_REALLOC_IN_PLACE_ONLY, p, 0);    /* shrinks in place, */
    T_TRUE(q == p);
    T_EQ(rt_heap_size(h, 0, q), 0);
    T_EQ(CALL(kernel32, HeapFree, h, 0, q), TRUE);      /* and is freed exactly once, by HeapFree */
    T_EQ(rt_heap_validate(h, 0, q), FALSE);
}
