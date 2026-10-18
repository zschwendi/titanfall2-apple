#include "rt/test.h"

/* ntohs keeps no state: it reads only its argument and never touches last error. The oracle is
 * the byte swap itself, checked on fixed values. */
TEST(wsock32, ntohs)
{
    T_CASE("swaps the two bytes of a network-order value");
    T_EQ(CALL(wsock32, ntohs, 0x3412), 0x1234);
    T_EQ(CALL(wsock32, ntohs, 0x9790), 37015);

    T_CASE("zero is unchanged");
    T_EQ(CALL(wsock32, ntohs, 0), 0);

    T_CASE("0xFFFF is unchanged");
    T_EQ(CALL(wsock32, ntohs, 0xFFFF), 0xFFFF);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(wsock32, ntohs, 0x3412);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
