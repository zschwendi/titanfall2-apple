#include "rt/test.h"
#include "rt/heap.h"

/* The oracle is the core's own heap: blocks come from rt_heap_alloc and are freed with
 * rt_heap_free, so the shim's answer is checked against what the heap really holds. */
TEST(kernel32, HeapValidate)
{
    HANDLE h = rt_heap_process();
    LPVOID p;
    int local[4] __attribute__((aligned(16)));

    T_CASE("a live block is valid");
    p = rt_heap_alloc(h, 0, 100);
    T_REQUIRE(p != NULL);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, p), TRUE);

    T_CASE("a freed block is invalid");
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, p), FALSE);

    T_CASE("an address 8 bytes into a live block is invalid");
    p = rt_heap_alloc(h, 0, 100);
    T_REQUIRE(p != NULL);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, (char *)p + 8), FALSE);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, p), TRUE);
    T_EQ(rt_heap_free(h, 0, p), TRUE);

    T_CASE("the address of a local variable is invalid");
    T_EQ(CALL(kernel32, HeapValidate, h, 0, local), FALSE);

    T_CASE("a NULL block validates the heap");
    T_EQ(CALL(kernel32, HeapValidate, h, 0, NULL), TRUE);

    T_CASE("a NULL heap handle is invalid");
    p = rt_heap_alloc(h, 0, 100);
    T_REQUIRE(p != NULL);
    T_EQ(CALL(kernel32, HeapValidate, NULL, 0, NULL), FALSE);
    T_EQ(CALL(kernel32, HeapValidate, NULL, 0, p), FALSE);

    T_CASE("HEAP_NO_SERIALIZE is accepted");
    T_EQ(CALL(kernel32, HeapValidate, h, HEAP_NO_SERIALIZE, p), TRUE);
    T_EQ(CALL(kernel32, HeapValidate, h, HEAP_NO_SERIALIZE, NULL), TRUE);
    T_EQ(CALL(kernel32, HeapValidate, h, HEAP_NO_SERIALIZE, local), FALSE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, p), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, local), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, (char *)p + 8), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, HeapValidate, NULL, 0, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    T_EQ(CALL(kernel32, HeapValidate, h, 0, p), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Refused: a flag bit HeapValidate does not take (the core stops on it).  Proved after the
     * checks of the last case; the flags are checked before the block, so NULL is enough. */
    T_TRAPS(CALL(kernel32, HeapValidate, h, 0x2, NULL), "which this function does not take");
}
