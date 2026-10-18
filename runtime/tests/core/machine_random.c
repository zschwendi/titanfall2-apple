/* rt_random_bytes (core/machine.c; the specification is rt/machine.h).
 *
 * The bytes have no expected values.  What is checked is what the specification promises:
 * exactly the bytes asked for are written, and they are not a constant.  The statistical
 * bounds are wide enough that a sound generator fails a run of this test less than once in
 * 10^12: a count of one byte value among 1,048,576 bytes has mean 4,096 and standard deviation
 * 64, and the bounds are 4,096 +- 512 (8 deviations); two equal draws of 32 bytes have
 * probability 2^-256.
 */
#include <string.h>

#include "rt/test.h"
#include "rt/machine.h"

TEST(core, machine_random)
{
    Ctx *ctx = T_CTX();
    rt_set_last_error(ctx, 0x5EA1);

    T_CASE("exactly the bytes asked for are written");
    {
        uint8_t buf[64], seen_or[32], seen_and[32];
        memset(seen_or, 0x00, sizeof seen_or);
        memset(seen_and, 0xFF, sizeof seen_and);
        for (unsigned round = 0; round < 64; round++) {
            memset(buf, 0xAA, sizeof buf);
            rt_random_bytes(buf + 16, 32);
            for (unsigned i = 0; i < 16; i++) { T_EQ(buf[i], 0xAA); T_EQ(buf[48 + i], 0xAA); }
            for (unsigned i = 0; i < 32; i++) { seen_or[i] |= buf[16 + i]; seen_and[i] &= buf[16 + i]; }
        }
        /* every one of the 32 positions was written: over 64 draws each bit of it was 0 once and 1 once */
        for (unsigned i = 0; i < 32; i++) { T_EQ(seen_or[i], 0xFF); T_EQ(seen_and[i], 0x00); }
        uint8_t one = 0xAA, guard = 0xAA;
        unsigned changed = 0;
        for (unsigned round = 0; round < 64; round++) { one = 0xAA; rt_random_bytes(&one, 1); changed += one != 0xAA; }
        T_TRUE(changed >= 32);                                   /* a single byte is written too */
        T_EQ(guard, 0xAA);
    }

    T_CASE("a length of 0 writes nothing, with or without a buffer");
    {
        uint8_t buf[8];
        memset(buf, 0xAA, sizeof buf);
        rt_random_bytes(buf, 0);
        rt_random_bytes(NULL, 0);
        for (unsigned i = 0; i < 8; i++) T_EQ(buf[i], 0xAA);
    }

    T_CASE("two draws differ and no byte value is missing or dominant");
    {
        static uint8_t big[1 << 20];
        uint8_t a[32], b[32];
        unsigned count[256] = { 0 }, low = 0, high = 0;
        rt_random_bytes(a, sizeof a);
        rt_random_bytes(b, sizeof b);
        T_TRUE(memcmp(a, b, sizeof a) != 0);
        rt_random_bytes(big, sizeof big);
        for (size_t i = 0; i < sizeof big; i++) count[big[i]]++;
        for (unsigned v = 0; v < 256; v++) { low += count[v] < 4096 - 512; high += count[v] > 4096 + 512; }
        T_EQ(low, 0);
        T_EQ(high, 0);
    }

    T_CASE("last error is never touched");
    T_EQ(T_LAST_ERROR(), 0x5EA1);

    T_CASE("no buffer for a length above 0 is fatal");
    T_TRAPS(rt_random_bytes(NULL, 16), "rt_random_bytes: NULL buffer for 16 bytes");
}
