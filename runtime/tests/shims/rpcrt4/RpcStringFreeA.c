#include <string.h>
#include "rt/test.h"
#include "rt/heap.h"
#include "rt/win/com.h"

TEST(rpcrt4, RpcStringFreeA)
{
    static const UUID u = { 0x00112233, 0x4455, 0x6677, { 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF } };
    BYTE *s, *stale, *m, *local_ptr, *other;
    _Alignas(16) BYTE local[64];          /* aligned, so the core gets past its alignment check */
    HANDLE fh;

    T_CASE("frees a string and sets the pointer to NULL");
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, &u, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    T_EQ(CALL(rpcrt4, RpcStringFreeA, &s), RPC_S_OK);
    T_TRUE(s == NULL);

    T_CASE("a second free of the NULL pointer returns RPC_S_OK");
    T_EQ(CALL(rpcrt4, RpcStringFreeA, &s), RPC_S_OK);
    T_TRUE(s == NULL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, &u, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    stale = s;                                      /* freed below; nothing is allocated before the trap that uses it */
    T_EQ(CALL(rpcrt4, RpcStringFreeA, &s), RPC_S_OK);
    T_TRUE(s == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS, after the checks of the last case.  The already-freed pointer comes first:
     * no block is allocated between its free and this trap. */
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, &stale), "was already freed");
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, NULL), "NULL String");

    /* A misaligned pointer into a live block; the block is then freed properly. */
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, &u, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    m = s + 1;
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, &m), "blocks are 16-byte aligned and this address is not");
    T_EQ(CALL(rpcrt4, RpcStringFreeA, &s), RPC_S_OK);

    /* A pointer outside guest memory: a host stack buffer. */
    local_ptr = local;
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, &local_ptr), "it was never allocated by the runtime heaps");

    /* A block of another heap.  Made as in HeapAlloc's test: a fixed heap. */
    fh = rt_heap_create(0, 0x200000, 0x200000);
    T_REQUIRE(fh != NULL);
    other = rt_heap_alloc(fh, 0, 64);
    T_REQUIRE(other != NULL);
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, &other), "the wrong heap");
    T_EQ(rt_heap_free(fh, 0, other), TRUE);
    T_EQ(CALL(kernel32, HeapDestroy, fh), TRUE);

    /* An interior pointer, 16 bytes into a live block, so it is aligned. */
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, &u, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    m = s + 16;
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, &m), "is an interior pointer");
    T_EQ(CALL(rpcrt4, RpcStringFreeA, &s), RPC_S_OK);

    /* A block of a heap that has been destroyed. */
    fh = rt_heap_create(0, 0x200000, 0x200000);
    T_REQUIRE(fh != NULL);
    other = rt_heap_alloc(fh, 0, 64);
    T_REQUIRE(other != NULL);
    T_EQ(CALL(kernel32, HeapDestroy, fh), TRUE);
    T_TRAPS(CALL(rpcrt4, RpcStringFreeA, &other), "belonged to a heap that was destroyed");
}
