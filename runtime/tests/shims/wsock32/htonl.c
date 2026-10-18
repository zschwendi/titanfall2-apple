#include "rt/test.h"
#include "rt/win/net.h"

TEST(wsock32, htonl)
{
    T_CASE("reverses the four bytes of the value");
    T_EQ(CALL(wsock32, htonl, 0x12345678), 0x78563412);

    T_CASE("zero");
    T_EQ(CALL(wsock32, htonl, 0), 0);

    T_CASE("all ones");
    T_EQ(CALL(wsock32, htonl, 0xFFFFFFFF), 0xFFFFFFFF);

    T_CASE("the loopback address");
    T_EQ(CALL(wsock32, htonl, INADDR_LOOPBACK), 0x0100007F);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(wsock32, htonl, 0x12345678), 0x78563412);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0);
    CALL(wsock32, htonl, INADDR_LOOPBACK);
    T_EQ(T_LAST_ERROR(), 0);
}
