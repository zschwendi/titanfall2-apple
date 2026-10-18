#include <string.h>
#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, HeapAlloc)
{
    HANDLE h = CALL(kernel32, GetProcessHeap);
    HANDLE fh;
    uint8_t *p, *q;
    unsigned nonzero;

    T_CASE("returns a 16-byte aligned block of the requested size");
    p = CALL(kernel32, HeapAlloc, h, 0, 100);
    T_TRUE(p != NULL);
    T_EQ((uintptr_t)p & 15, 0);
    T_EQ(rt_heap_size(h, 0, p), 100);
    memset(p, 0x5A, 100);                                   /* all 100 bytes are usable */
    T_TRUE(p[0] == 0x5A && p[99] == 0x5A);
    T_EQ(rt_heap_free(h, 0, p), TRUE);

    T_CASE("HEAP_ZERO_MEMORY zeroes the requested bytes");
    p = CALL(kernel32, HeapAlloc, h, 0, 1000);              /* dirty a block the zone may hand out again */
    T_TRUE(p != NULL);
    memset(p, 0xAB, 1000);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    p = CALL(kernel32, HeapAlloc, h, HEAP_ZERO_MEMORY, 1000);
    T_TRUE(p != NULL);
    nonzero = 0;
    for (unsigned i = 0; i < 1000; i++) if (p[i]) nonzero++;
    T_EQ(nonzero, 0);
    T_EQ(rt_heap_free(h, 0, p), TRUE);

    T_CASE("a size of 0 gives a valid block");
    p = CALL(kernel32, HeapAlloc, h, 0, 0);
    q = CALL(kernel32, HeapAlloc, h, 0, 0);
    T_TRUE(p != NULL);
    T_TRUE(q != NULL && q != p);
    T_EQ(rt_heap_size(h, 0, p), 0);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(rt_heap_free(h, 0, q), TRUE);

    T_CASE("HEAP_NO_SERIALIZE is accepted");
    p = CALL(kernel32, HeapAlloc, h, HEAP_NO_SERIALIZE, 64);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 64);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    /* The refused flags, in the same case: a bit rt/heap.h does not know ... */
    T_TRAPS(CALL(kernel32, HeapAlloc, h, 0x2, 64), "flag bits that are not in rt/heap.h");
    /* ... and a bit it knows but HeapAlloc does not take (the core stops on it). */
    T_TRAPS(CALL(kernel32, HeapAlloc, h, HEAP_CREATE_ENABLE_EXECUTE, 64), "which this function does not take");

    /* A failed HeapAlloc sets ERROR_NOT_ENOUGH_MEMORY.  Windows: Wine kernel32/tests/heap.c
     * test_HeapCreate, fixed-size heaps: last error preset to 0xdeadbeef; an allocation that cannot
     * fit returns NULL and last error must be ERROR_NOT_ENOUGH_MEMORY (three places, none marked
     * broken or todo_wine; the test says the same runs on 64-bit Windows 7).  The Microsoft page
     * says HeapAlloc does not call SetLastError; the test contradicts the page. */
    T_CASE("a request that cannot be met returns NULL and sets ERROR_NOT_ENOUGH_MEMORY");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    p = CALL(kernel32, HeapAlloc, h, 0, (SIZE_T)1 << 42);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    /* In a fixed-size heap: a block of 0x100000 is over the 0xFF000 bytes such a heap can hold (that
     * figure is ours, rt/heap.h).  Windows, 64-bit (Wine kernel32/tests/heap.c test_HeapCreate): this
     * block is refused, and a block of 0x80000 fits, as the cases below check.
     * The heap is made with rt_heap_create, not CALL(HeapCreate): the HeapCreate shim stops on an
     * initial size equal to the maximum (its own test asserts that), while rt_heap_create accepts
     * it (rt/heap.h), and Wine's test makes this heap.  The sizes are the block's. */
    fh = rt_heap_create(0, 0x200000, 0x200000);
    T_TRUE(fh != NULL);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    p = CALL(kernel32, HeapAlloc, fh, 0, 0x100000);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    /* The same heap still serves a block that fits, and a success leaves last error alone. */
    p = CALL(kernel32, HeapAlloc, fh, 0, 0x40000);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(fh, 0, p), 0x40000);
    memset(p, 0x5A, 0x40000);                               /* all 0x40000 bytes are usable */
    T_TRUE(p[0] == 0x5A && p[0x3FFFF] == 0x5A);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(rt_heap_free(fh, 0, p), TRUE);
    /* A block of 0x80000, which 64-bit Windows holds in that heap, succeeds, and last error stays. */
    p = CALL(kernel32, HeapAlloc, fh, 0, 0x80000);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(fh, 0, p), 0x80000);
    memset(p, 0x3C, 0x80000);
    T_TRUE(p[0] == 0x3C && p[0x7FFFF] == 0x3C);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(rt_heap_free(fh, 0, p), TRUE);
    T_EQ(CALL(kernel32, HeapDestroy, fh), TRUE);

    T_CASE("HEAP_GENERATE_EXCEPTIONS makes that failure fatal");
    T_TRUE(h != NULL);
    T_TRAPS(CALL(kernel32, HeapAlloc, h, HEAP_GENERATE_EXCEPTIONS, (SIZE_T)1 << 42),
            "HEAP_GENERATE_EXCEPTIONS is set");

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, HeapAlloc, h, 0, 16);
    T_TRUE(p != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
}
