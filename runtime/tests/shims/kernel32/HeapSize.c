#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, HeapSize)
{
    HANDLE h = CALL(kernel32, GetProcessHeap);
    LPVOID p;
    int local[4] __attribute__((aligned(16)));

    T_CASE("returns the requested size of a live block, exactly");
    p = CALL(kernel32, HeapAlloc, h, 0, 100);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, HeapSize, h, 0, p), 100);
    T_EQ(CALL(kernel32, HeapSize, h, HEAP_NO_SERIALIZE, p), 100);
    /* Not rounded up to the zone's block size: a large block reports what was asked for. */
    LPVOID big = CALL(kernel32, HeapAlloc, h, 0, 100000);
    T_TRUE(big != NULL);
    T_EQ(CALL(kernel32, HeapSize, h, 0, big), 100000);
    T_EQ(CALL(kernel32, HeapFree, h, 0, big), TRUE);
    /* Refused, in the same case: a bit rt/heap.h does not know ... */
    T_TRAPS(CALL(kernel32, HeapSize, h, 0x2, p), "flag bits that are not in rt/heap.h");
    /* ... and a bit it knows but HeapSize does not take (the core stops on it). */
    T_TRAPS(CALL(kernel32, HeapSize, h, HEAP_ZERO_MEMORY, p), "which this function does not take");
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    T_CASE("a size of 0 gives 0");
    p = CALL(kernel32, HeapAlloc, h, 0, 0);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, HeapSize, h, 0, p), 0);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);

    T_CASE("NULL and a pointer that is not a block of this heap return (SIZE_T)-1");
    T_EQ(CALL(kernel32, HeapSize, h, 0, NULL), (SIZE_T)-1);
    T_EQ(CALL(kernel32, HeapSize, h, 0, local), (SIZE_T)-1);

    T_CASE("does not change last error");
    p = CALL(kernel32, HeapAlloc, h, 0, 16);
    T_TRUE(p != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapSize, h, 0, p), 16);
    T_EQ(T_LAST_ERROR(), 0x1234);
    /* The documented failure does not call SetLastError either. */
    T_EQ(CALL(kernel32, HeapSize, h, 0, NULL), (SIZE_T)-1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);
}
