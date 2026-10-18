#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, HeapCreate)
{
    HANDLE h;
    LPVOID p;

    T_CASE("a growable heap when the maximum size is 0");
    h = CALL(kernel32, HeapCreate, 0, 0, 0);
    T_TRUE(h != NULL);
    /* A growable heap takes a block larger than a fixed-size heap's block limit. */
    p = CALL(kernel32, HeapAlloc, h, 0, 1 << 20);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 1 << 20);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
    /* Refused, in the same case: a size above the guest address space, as the initial or the maximum size. */
    T_TRAPS(CALL(kernel32, HeapCreate, 0, (SIZE_T)1 << 50, 0), "a size above the guest address space");
    T_TRAPS(CALL(kernel32, HeapCreate, 0, 0, (SIZE_T)1 << 50), "a size above the guest address space");

    T_CASE("a fixed-size heap when the initial size is smaller than the maximum");
    h = CALL(kernel32, HeapCreate, 0, 0x1000, 0x10000);
    T_TRUE(h != NULL);
    p = CALL(kernel32, HeapAlloc, h, 0, 0x100);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
    /* Refused, in the same case: an initial size not smaller than the maximum, equal or larger. */
    T_TRAPS(CALL(kernel32, HeapCreate, 0, 0x2000, 0x1000), "not smaller than the maximum size");
    T_TRAPS(CALL(kernel32, HeapCreate, 0, 0x1000, 0x1000), "not smaller than the maximum size");

    T_CASE("an initial size of 0 gives a working heap");
    h = CALL(kernel32, HeapCreate, 0, 0, 0x10000);
    T_TRUE(h != NULL);
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);

    T_CASE("HEAP_NO_SERIALIZE and HEAP_CREATE_ENABLE_EXECUTE are accepted");
    h = CALL(kernel32, HeapCreate, HEAP_NO_SERIALIZE | HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
    T_TRUE(h != NULL);
    p = CALL(kernel32, HeapAlloc, h, 0, 64);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
    /* Refused, in the same case: a bit rt/heap.h does not know ... */
    T_TRAPS(CALL(kernel32, HeapCreate, 0x2, 0, 0), "flag bits that are not in rt/heap.h");
    /* ... and a bit it knows but HeapCreate does not take (the core stops on it). */
    T_TRAPS(CALL(kernel32, HeapCreate, HEAP_REALLOC_IN_PLACE_ONLY, 0, 0), "which this function does not take");

    T_CASE("HEAP_GENERATE_EXCEPTIONS applies to every call on the heap");
    h = CALL(kernel32, HeapCreate, HEAP_GENERATE_EXCEPTIONS, 0, 0);
    T_TRUE(h != NULL);
    /* The heap's option makes a failed HeapAlloc fatal, though the call has no flags. */
    T_TRAPS(CALL(kernel32, HeapAlloc, h, 0, (SIZE_T)1 << 42), "HEAP_GENERATE_EXCEPTIONS is set");
    T_EQ(rt_heap_destroy(h), TRUE);

    T_CASE("a fixed-size heap returns NULL for a block that does not fit its maximum");
    h = CALL(kernel32, HeapCreate, 0, 0x10000, 0x20000);
    T_TRUE(h != NULL);
    p = CALL(kernel32, HeapAlloc, h, 0, 0x1000);
    T_TRUE(p != NULL);
    T_TRUE(CALL(kernel32, HeapAlloc, h, 0, 0x30000) == NULL);   /* 192 KB in a 128 KB heap */
    T_EQ(CALL(kernel32, HeapFree, h, 0, p), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, HeapCreate, 0, 0, 0);
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_destroy(h), TRUE);
}
