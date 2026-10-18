#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, HeapDestroy)
{
    HANDLE h, p0;
    LPVOID p;
    int local[4] __attribute__((aligned(16)));

    T_CASE("destroys a heap and returns TRUE");
    h = CALL(kernel32, HeapCreate, 0, 0, 0);
    T_TRUE(h != NULL);
    T_EQ(CALL(kernel32, HeapDestroy, h), TRUE);

    T_CASE("a heap's blocks go with it, without HeapFree");
    h = CALL(kernel32, HeapCreate, 0, 0, 0);
    T_TRUE(h != NULL);
    p = CALL(kernel32, HeapAlloc, h, 0, 100);
    T_TRUE(p != NULL);
    p = CALL(kernel32, HeapAlloc, h, 0, 100000);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, HeapDestroy, h), TRUE);
    /* Refused, in the same case: a handle that is destroyed already ... */
    T_TRAPS(CALL(kernel32, HeapDestroy, h), "already destroyed");
    /* ... and a pointer that is not a heap handle. */
    T_TRAPS(CALL(kernel32, HeapDestroy, (HANDLE)local), "is not a heap handle");

    T_CASE("does not change last error on success");
    h = CALL(kernel32, HeapCreate, 0, 0, 0);
    T_TRUE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapDestroy, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the process heap fails with ERROR_INVALID_HANDLE and nothing is destroyed");
    p0 = CALL(kernel32, GetProcessHeap);
    p = CALL(kernel32, HeapAlloc, p0, 0, 32);
    T_TRUE(p != NULL);
    for (int i = 0; i < 32; i++) ((uint8_t *)p)[i] = (uint8_t)(0x40 + i);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapDestroy, p0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, HeapDestroy, p0), FALSE);       /* every time, not only the first */
    T_TRUE(CALL(kernel32, GetProcessHeap) == p0);       /* the heap is still the process heap ... */
    T_EQ(rt_heap_size(p0, 0, p), 32);                   /* ... and its blocks are live and intact */
    for (int i = 0; i < 32; i++) T_EQ(((uint8_t *)p)[i], (uint8_t)(0x40 + i));
    T_EQ(CALL(kernel32, HeapFree, p0, 0, p), TRUE);
}
