#include "rt/test.h"
#include "rt/net.h"
#include "rt/thread.h"

/* Sockets are made and closed with the core's own calls (rt/net.h), not through another shim.
 * The stop case comes last: it ends the run, and each test is its own process. */
TEST(wsock32, ioctlsocket)
{
    WSADATA d;
    SOCKET s, closed;
    struct { ULONG v; ULONG guard; } buf;
    ULONG one;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    s = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(s != INVALID_SOCKET);

    T_CASE("FIONBIO with 1 and with 0 returns 0 and leaves last error alone");
    rt_thread_set_last_error(0x1234);
    one = 1;
    T_EQ(CALL(wsock32, ioctlsocket, s, FIONBIO, &one), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(one, 1);                       /* the argument is only read */
    one = 0;
    T_EQ(CALL(wsock32, ioctlsocket, s, FIONBIO, &one), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(one, 0);

    T_CASE("FIONREAD on a new socket stores 0");
    buf.v = 0xDEAD;
    buf.guard = 0xCAFEBABE;
    rt_thread_set_last_error(0x1234);
    T_EQ(CALL(wsock32, ioctlsocket, s, FIONREAD, &buf.v), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(buf.v, 0);
    T_EQ(buf.guard, 0xCAFEBABE);        /* only the four bytes of the ULONG are written */

    T_CASE("argp NULL fails with WSAEFAULT");
    T_EQ(CALL(wsock32, ioctlsocket, s, FIONBIO, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(CALL(wsock32, ioctlsocket, s, FIONREAD, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("a value that is not a socket fails with WSAENOTSOCK");
    closed = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(closed != INVALID_SOCKET);
    T_EQ(rt_net_close(closed), 0);
    one = 1;
    T_EQ(CALL(wsock32, ioctlsocket, closed, FIONBIO, &one), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, ioctlsocket, (SOCKET)0, FIONREAD, &buf.v), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, ioctlsocket, INVALID_SOCKET, FIONBIO, &one), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, ioctlsocket, s | ((SOCKET)1 << 32), FIONBIO, &one), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_TRAPS(CALL(wsock32, ioctlsocket, s, 0x12345678, &one), "rt_net_ioctl: not implemented");
}
