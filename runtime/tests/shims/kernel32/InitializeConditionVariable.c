#include "rt/test.h"

TEST(kernel32, InitializeConditionVariable)
{
    CONDITION_VARIABLE cv;
    uint64_t bits;

    T_CASE("the variable is set to its initial state, all zero");
    memset(&cv, 0xFF, sizeof cv);
    CALL(kernel32, InitializeConditionVariable, &cv);
    memcpy(&bits, &cv, sizeof bits);
    T_EQ(bits, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InitializeConditionVariable, &cv);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
