#include "rt/test.h"
#include "rt/win/com.h"

/* The 16 bytes of 00112233-4455-6677-8899-aabbccddeeff in memory (little endian): Data1,
 * Data2 and Data3 reversed, then Data4 as written. */
static const BYTE u_want[16] = {
    0x33, 0x22, 0x11, 0x00,
    0x55, 0x44,
    0x77, 0x66,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

/* Preset: every byte of *u set to v. */
static void u_fill(UUID *u, BYTE v)
{
    BYTE *b = (BYTE *)u;
    for (unsigned i = 0; i < sizeof *u; i++) b[i] = v;
}

/* Number of bytes of *u that are not v. */
static unsigned u_other_than(const UUID *u, BYTE v)
{
    const BYTE *b = (const BYTE *)u;
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof *u; i++) if (b[i] != v) n++;
    return n;
}

/* Number of bytes of *u that differ from want[]. */
static unsigned u_differing(const UUID *u, const BYTE *want)
{
    const BYTE *b = (const BYTE *)u;
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof *u; i++) if (b[i] != want[i]) n++;
    return n;
}

TEST(rpcrt4, UuidFromStringA)
{
    UUID u;
    RPC_STATUS st;

    T_CASE("lower-case string gives the members");
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"00112233-4455-6677-8899-aabbccddeeff", &u);
    T_EQ(st, RPC_S_OK);
    T_EQ(u.Data1, 0x00112233);
    T_EQ(u.Data2, 0x4455);
    T_EQ(u.Data3, 0x6677);
    T_EQ(u_differing(&u, u_want), 0);

    T_CASE("upper-case string gives the members");
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"00112233-4455-6677-8899-AABBCCDDEEFF", &u);
    T_EQ(st, RPC_S_OK);
    T_EQ(u.Data1, 0x00112233);
    T_EQ(u.Data2, 0x4455);
    T_EQ(u.Data3, 0x6677);
    T_EQ(u_differing(&u, u_want), 0);

    T_CASE("NULL string stores the nil UUID");
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, NULL, &u);
    T_EQ(st, RPC_S_OK);
    T_EQ(u_other_than(&u, 0), 0);

    T_CASE("wrong length fails and leaves Uuid untouched");
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"00112233-4455-6677-8899-aabbccddeef", &u);     /* 35 */
    T_EQ(st, RPC_S_INVALID_STRING_UUID);
    T_EQ(u_other_than(&u, 0xA5), 0);
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"00112233-4455-6677-8899-aabbccddeeff0", &u);    /* 37 */
    T_EQ(st, RPC_S_INVALID_STRING_UUID);
    T_EQ(u_other_than(&u, 0xA5), 0);

    T_CASE("non-hex digit fails and leaves Uuid untouched");
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"0011g233-4455-6677-8899-aabbccddeeff", &u);
    T_EQ(st, RPC_S_INVALID_STRING_UUID);
    T_EQ(u_other_than(&u, 0xA5), 0);

    T_CASE("hyphen in the wrong place fails and leaves Uuid untouched");
    u_fill(&u, 0xA5);
    st = CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"001122334-455-6677-8899-aabbccddeeff", &u);
    T_EQ(st, RPC_S_INVALID_STRING_UUID);
    T_EQ(u_other_than(&u, 0xA5), 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"00112233-4455-6677-8899-aabbccddeeff", &u);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"0011g233-4455-6677-8899-aabbccddeeff", &u);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(rpcrt4, UuidFromStringA, NULL, &u);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(rpcrt4, UuidFromStringA, (RPC_CSTR)"00112233-4455-6677-8899-aabbccddeeff", NULL),
            "UuidFromStringA with NULL Uuid");
}
