#include "rt/test.h"

TEST(kernel32, GetOEMCP)
{
    T_CASE("returns the OEM code page of the en-US machine (437)");
    T_EQ(CALL(kernel32, GetOEMCP), 437);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetOEMCP);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
