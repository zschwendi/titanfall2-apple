#include "rt/test.h"

TEST(kernel32, SetHandleCount)
{
    T_CASE("returns the number of file handles requested");
    T_EQ(CALL(kernel32, SetHandleCount, 0), 0);
    T_EQ(CALL(kernel32, SetHandleCount, 20), 20);
    T_EQ(CALL(kernel32, SetHandleCount, 255), 255);
    T_EQ(CALL(kernel32, SetHandleCount, 0xFFFFFFFF), 0xFFFFFFFF);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetHandleCount, 20), 20);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, SetHandleCount, 255), 255);
    T_EQ(T_LAST_ERROR(), 0);
}
