#include "rt/test.h"

/* Each case takes its own block from GetEnvironmentStringsW and frees it once. */
TEST(kernel32, FreeEnvironmentStringsW)
{
    WCHAR *block;

    T_CASE("frees a block from GetEnvironmentStringsW and returns TRUE");
    block = CALL(kernel32, GetEnvironmentStringsW);
    T_TRUE(block != NULL);
    T_EQ(CALL(kernel32, FreeEnvironmentStringsW, block), TRUE);

    T_CASE("does not change last error");
    block = CALL(kernel32, GetEnvironmentStringsW);
    T_TRUE(block != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FreeEnvironmentStringsW, block), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL penv returns TRUE and does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FreeEnvironmentStringsW, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
