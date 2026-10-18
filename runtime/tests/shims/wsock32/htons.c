#include "rt/test.h"

TEST(wsock32, htons)
{
    T_CASE("swaps the two bytes of a 16-bit value");
    T_EQ(CALL(wsock32, htons, 0x1234), 0x3412);
    T_EQ(CALL(wsock32, htons, 37015), 0x9790);      /* 0x9097, the engine's server port */

    T_CASE("zero and 0xFFFF come back unchanged");
    T_EQ(CALL(wsock32, htons, 0), 0);
    T_EQ(CALL(wsock32, htons, 0xFFFF), 0xFFFF);

    T_CASE("does not change last error");
    rt_set_last_error(T_CTX(), 0xDEAD);
    T_EQ(CALL(wsock32, htons, 0x1234), 0x3412);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
}
