#include "rt/test.h"

/* Each case takes its own block from GetEnvironmentStringsA and frees it once. */
TEST(kernel32, FreeEnvironmentStringsA)
{
    CHAR *block;

    T_CASE("frees a block from GetEnvironmentStringsA and returns TRUE");
    block = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(block != NULL);
    T_EQ(CALL(kernel32, FreeEnvironmentStringsA, block), TRUE);

    T_CASE("does not change last error");
    block = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(block != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FreeEnvironmentStringsA, block), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL penv returns TRUE and does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FreeEnvironmentStringsA, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop: after the checks of the last case.  The block was freed above, so this second
     * free is of a pointer that is not a live block; the report names the pointer. */
    T_TRAPS(CALL(kernel32, FreeEnvironmentStringsA, block), "is not a block from GetEnvironmentStrings");
}
