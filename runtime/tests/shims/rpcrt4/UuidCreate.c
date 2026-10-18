#include <string.h>

#include "rt/test.h"
#include "rt/win/com.h"

/* The bytes are random, so the checks are on what they are like, never on their values.  The
 * UUID lies between two guard areas: a write before or after it changes a guard byte. */
#define FILL   0xABu
#define GUARD  0xCCu
#define ROUNDS 8

typedef struct {
    BYTE pre[8];
    UUID u;
    BYTE post[8];
} Framed;

TEST(rpcrt4, UuidCreate)
{
    Framed f;
    UUID first, second;
    BYTE written[16];
    RPC_STATUS st;
    int i, r;

    T_CASE("writes a version-4 random UUID and returns RPC_S_OK");
    for (r = 0; r < ROUNDS; r++) {
        memset(&f.u, FILL, sizeof f.u);
        st = CALL(rpcrt4, UuidCreate, &f.u);
        T_EQ(st, RPC_S_OK);
        T_EQ(f.u.Data3 & 0xF000, 0x4000);
        T_EQ(f.u.Data4[0] & 0xC0, 0x80);
    }

    T_CASE("writes exactly the sixteen bytes of the UUID");
    memset(written, 0, sizeof written);
    for (r = 0; r < ROUNDS; r++) {
        memset(&f, GUARD, sizeof f);
        memset(&f.u, FILL, sizeof f.u);
        CALL(rpcrt4, UuidCreate, &f.u);
        for (i = 0; i < 8; i++) {
            T_EQ(f.pre[i], GUARD);
            T_EQ(f.post[i], GUARD);
        }
        for (i = 0; i < 16; i++) {
            if (((const BYTE *)&f.u)[i] != FILL) written[i] = 1;
        }
    }
    for (i = 0; i < 16; i++) T_TRUE(written[i]);

    T_CASE("two calls give different UUIDs");
    memset(&first, 0, sizeof first);
    memset(&second, 0, sizeof second);
    T_EQ(CALL(rpcrt4, UuidCreate, &first), RPC_S_OK);
    T_EQ(CALL(rpcrt4, UuidCreate, &second), RPC_S_OK);
    T_TRUE(memcmp(&first, &second, sizeof first) != 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(rpcrt4, UuidCreate, &first);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(rpcrt4, UuidCreate, NULL), "NULL Uuid");
}
