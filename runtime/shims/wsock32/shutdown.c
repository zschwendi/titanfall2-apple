/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-shutdown
 * RT-CASES:  a stream socket gets WSAENOTCONN; how 3 gets WSAEINVAL; a datagram socket with SD_SEND gives 0 and a following rt_net_sendto gets WSAESHUTDOWN; not a socket gives WSAENOTSOCK; a call that succeeds sets last error to 0
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 22, int, shutdown, SOCKET s, int how)
{
    /* The core checks the socket and `how`, closes the directions for datagram sockets and
     * sets last error (rt_net_shutdown, rt/net.h).  The shim adds no guard. */
    return rt_net_shutdown(s, how);
}
