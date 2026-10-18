#include "rt/test.h"

TEST(kernel32, AreFileApisANSI)
{
    T_CASE("returns TRUE on each of two calls");
    T_EQ(CALL(kernel32, AreFileApisANSI), TRUE);
    T_EQ(CALL(kernel32, AreFileApisANSI), TRUE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, AreFileApisANSI), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
