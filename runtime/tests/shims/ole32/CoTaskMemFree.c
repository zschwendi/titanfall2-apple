#include "rt/test.h"
#include "rt/heap.h"

/* The oracle is the core's own heap: rt_heap_validate says whether a block is live, so a
 * free shows as the block going from live to not live. */
TEST(ole32, CoTaskMemFree)
{
    HANDLE h = rt_heap_process();
    LPVOID p;
    int local[4] __attribute__((aligned(16)));
    int i, bad;

    T_CASE("NULL has no effect");
    p = rt_galloc(32);
    T_REQUIRE(p != NULL);
    memset(p, 0x5A, 32);
    CALL(ole32, CoTaskMemFree, NULL);
    T_TRUE(rt_heap_validate(h, 0, p));
    for (i = 0, bad = 0; i < 32; i++) bad += ((unsigned char *)p)[i] != 0x5A;
    T_EQ(bad, 0);
    CALL(ole32, CoTaskMemFree, p);

    T_CASE("frees a block from rt_galloc");
    p = rt_galloc(32);
    T_REQUIRE(p != NULL);
    T_TRUE(rt_heap_validate(h, 0, p));
    CALL(ole32, CoTaskMemFree, p);
    T_EQ(rt_heap_validate(h, 0, p), FALSE);

    T_CASE("does not change last error");
    p = rt_galloc(32);
    T_REQUIRE(p != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ole32, CoTaskMemFree, p);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ole32, CoTaskMemFree, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* After the checks of the last case: pointers that are not a live block are fatal in the
     * core (the stop the notes name), so the shim does not guard them. */
    p = rt_galloc(32);
    T_REQUIRE(p != NULL);
    CALL(ole32, CoTaskMemFree, p);
    T_TRAPS(CALL(ole32, CoTaskMemFree, p), "is not a block of heap");                     /* a second free */
    T_TRAPS(CALL(ole32, CoTaskMemFree, local), "is not a block of heap");                 /* a stack address, not from rt_galloc */
    p = rt_galloc(32);
    T_REQUIRE(p != NULL);
    T_TRAPS(CALL(ole32, CoTaskMemFree, (char *)p + 1), "is not a block of heap");         /* a misaligned pointer into a live block */
    CALL(ole32, CoTaskMemFree, p);
    {
        HANDLE other = rt_heap_create(0, 0, 0);
        LPVOID q;
        T_REQUIRE(other != NULL);
        q = rt_heap_alloc(other, 0, 32);
        T_REQUIRE(q != NULL);
        T_TRAPS(CALL(ole32, CoTaskMemFree, q), "it is a block of heap");                  /* a block of another heap */
        T_EQ(rt_heap_free(other, 0, q), TRUE);
        T_EQ(rt_heap_destroy(other), TRUE);
    }
}
