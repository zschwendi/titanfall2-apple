#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, HeapFree)
{
    HANDLE h = CALL(kernel32, GetProcessHeap);
    LPVOID p;
    int local[4] __attribute__((aligned(16)));

    T_CASE("frees a live block and returns TRUE");
    p = CALL(kernel32, HeapAlloc, h, 0, 100);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);
    /* Pointers that are not live blocks of this heap are fatal, and the report names them. */
    T_TRAPS(CALL(kernel32, HeapFree, h, 0, p), "is not a block of heap");                 /* a second free */
    T_TRAPS(CALL(kernel32, HeapFree, h, 0, local), "is not a block of heap");             /* a stack address */
    {
        HANDLE other = rt_heap_create(0, 0, 0);
        LPVOID q;
        T_REQUIRE(other != NULL);
        q = rt_heap_alloc(other, 0, 32);
        T_REQUIRE(q != NULL);
        T_TRAPS(CALL(kernel32, HeapFree, h, 0, q), "it is a block of heap");              /* a block of another heap */
        T_EQ(rt_heap_free(other, 0, q), TRUE);
        T_EQ(rt_heap_destroy(other), TRUE);
    }

    T_CASE("NULL is accepted and returns TRUE");
    T_EQ(CALL(kernel32, HeapFree, h, 0, NULL), TRUE);
    /* A NULL heap handle with a NULL block is TRUE too, with no fatal report (Windows: Wine
     * kernel32/tests/heap.c test_HeapCreate, "test some border cases": HeapFree(NULL, 0, NULL)
     * and HeapFree(heap, 0, NULL) both return TRUE). */
    T_EQ(CALL(kernel32, HeapFree, NULL, 0, NULL), TRUE);

    T_CASE("HEAP_NO_SERIALIZE is accepted");
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, HeapFree, h, HEAP_NO_SERIALIZE, p), TRUE);
    /* Refused, in the same case: a bit rt/heap.h does not know ... */
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    T_TRAPS(CALL(kernel32, HeapFree, h, 0x2, p), "flag bits that are not in rt/heap.h");
    /* ... and a bit it knows but HeapFree does not take (the core stops on it). */
    T_TRAPS(CALL(kernel32, HeapFree, h, HEAP_ZERO_MEMORY, p), "which this function does not take");
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    T_CASE("does not change last error");
    p = CALL(kernel32, HeapAlloc, h, 0, 16);
    T_TRUE(p != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapFree, h, 0, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapFree, NULL, 0, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
