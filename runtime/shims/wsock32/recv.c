/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-recv
 * RT-CASES:  stream socket gets WSAENOTCONN; not a socket gets WSAENOTSOCK; unbound datagram socket gets WSAEINVAL; bound non-blocking datagram socket with nothing queued gets WSAEWOULDBLOCK; a queued datagram comes back with its length; a datagram longer than len gets WSAEMSGSIZE and the rest is lost; an empty datagram comes back as 0; len zero or negative, or buf NULL: WSAEFAULT; shut down for receiving: WSAESHUTDOWN; success sets last error to 0
 * RT-TRAPS:  core: flags other than 0; core: blocking receive with nothing queued
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 16, int, recv, SOCKET s, char *buf, int len, int flags)
{
    return rt_net_recvfrom(s, buf, len, flags, NULL, NULL);
}
