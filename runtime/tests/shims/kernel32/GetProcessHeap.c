#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, GetProcessHeap)
{
    HANDLE h;

    T_CASE("returns the process heap");
    h = CALL(kernel32, GetProcessHeap);
    T_TRUE(h != NULL);
    T_EQ(h, rt_heap_process());
    /* It is a working heap: a block can be allocated from it and freed. */
    void *p = rt_heap_alloc(h, 0, 32);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_free(h, 0, p), TRUE);

    T_CASE("the same handle on every call");
    T_EQ(CALL(kernel32, GetProcessHeap), h);
    T_EQ(CALL(kernel32, GetProcessHeap), CALL(kernel32, GetProcessHeap));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetProcessHeap);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
