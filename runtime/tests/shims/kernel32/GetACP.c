#include "rt/test.h"

TEST(kernel32, GetACP)
{
    T_CASE("returns the ANSI code page of the en-US machine (1252)");
    T_EQ(CALL(kernel32, GetACP), 1252);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetACP);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
