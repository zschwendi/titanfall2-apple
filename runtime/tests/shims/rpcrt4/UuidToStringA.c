#include <string.h>
#include "rt/test.h"
#include "rt/heap.h"
#include "rt/win/com.h"

TEST(rpcrt4, UuidToStringA)
{
    static const UUID u = { 0x00112233, 0x4455, 0x6677, { 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF } };
    BYTE *s;

    T_CASE("converts a UUID to 36 lower-case characters and a NUL");
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, &u, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    T_EQ(strlen((char *)s), 36);                    /* the NUL is at s[36] ... */
    T_TRUE(strcmp((char *)s, "00112233-4455-6677-8899-aabbccddeeff") == 0);   /* ... and every char before it */
    T_EQ(rt_heap_free(rt_heap_process(), 0, s), TRUE);

    T_CASE("a NULL UUID is the nil UUID");
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, NULL, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    T_EQ(strlen((char *)s), 36);
    T_TRUE(strcmp((char *)s, "00000000-0000-0000-0000-000000000000") == 0);
    T_EQ(rt_heap_free(rt_heap_process(), 0, s), TRUE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    s = NULL;
    T_EQ(CALL(rpcrt4, UuidToStringA, &u, &s), RPC_S_OK);
    T_REQUIRE(s != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_free(rt_heap_process(), 0, s), TRUE);

    /* RT-TRAPS: NULL StringUuid, after the checks of the last case */
    T_TRAPS(CALL(rpcrt4, UuidToStringA, &u, NULL), "NULL StringUuid");
}
