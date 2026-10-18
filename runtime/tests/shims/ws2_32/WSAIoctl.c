#include "rt/test.h"

/* No case: every call is refused. 0x98000004 is SIO_KEEPALIVE_VALS, the code libcurl passes. */
TEST(ws2_32, WSAIoctl)
{
    T_TRAPS(CALL(ws2_32, WSAIoctl, 68, 0x98000004u, NULL, 0, NULL, 0, NULL, NULL, (GUESTFN)0),
            "WSAIoctl(68, 0x98000004)");
}
