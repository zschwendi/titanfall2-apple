#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, LocalFree)
{
    HANDLE h = rt_heap_process();
    uint8_t *p;

    T_CASE("NULL is ignored and returns NULL");
    T_TRUE(CALL(kernel32, LocalFree, NULL) == NULL);

    T_CASE("a block of the process heap is freed and returns NULL");
    p = CALL(kernel32, LocalAlloc, LPTR, 32);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 32);
    T_TRUE(CALL(kernel32, LocalFree, p) == NULL);
    T_EQ(rt_heap_validate(h, 0, p), FALSE);                 /* freed: no longer a live block */

    T_CASE("does not change last error on success");
    p = CALL(kernel32, LocalAlloc, LMEM_FIXED, 16);
    T_TRUE(p != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(kernel32, LocalFree, p) == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* A pointer that is not a live block is the core's stop: here a block that was freed above
     * (a double free; the core's report says so).  The shim adds no guard for it. */
    T_TRAPS(CALL(kernel32, LocalFree, p), "already freed (a double free");
}
