/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-recvfrom
 * RT-CASES:  a bound non-blocking socket with nothing queued gives WSAEWOULDBLOCK ten times in a row, leaving from and fromlen untouched; a queued datagram comes back with its bytes, its sender's address and port, and fromlen 16; a call that succeeds sets last error to 0; a datagram longer than len gives WSAEMSGSIZE, and the next call WSAEWOULDBLOCK; an unbound socket gives WSAEINVAL; a stream socket that is not connected gives WSAENOTCONN; not a socket gives WSAENOTSOCK
 * RT-TRAPS:  core: flags other than 0; core: a blocking receive with nothing queued
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 17, int, recvfrom, SOCKET s, char *buf, int len, int flags, SOCKADDR *from, int *fromlen)
{
    /* The core checks the arguments, queues and dequeues datagrams and sets last error
     * (rt_net_recvfrom, rt/net.h).  The shim adds no guard: the core's stops stay stops. */
    return rt_net_recvfrom(s, buf, len, flags, from, fromlen);
}
