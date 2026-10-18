/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-connect
 * RT-CASES:  stream socket to 127.0.0.1 port 80 gives WSAECONNREFUSED, blocking and non-blocking; stream socket to 192.0.2.1 port 80 gives WSAENETUNREACH, blocking and non-blocking; datagram socket to 127.0.0.1 port 4000 gives 0 and leaves last error alone, and getpeername reports it; connecting the datagram socket to a zeroed AF_UNSPEC name gives 0 and removes the peer; stream socket to 0.0.0.0 port 80 gives WSAEADDRNOTAVAIL; not a socket gets WSAENOTSOCK
 * RT-TRAPS:  core: an AF_UNSPEC name on a stream socket; core: an AF_UNSPEC name shorter than 16 bytes; core: a datagram socket connected to port 0; core: a datagram socket connected to the unspecified address; core: an IPv4-mapped peer on a datagram socket with IPV6_V6ONLY 1
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 4, int, connect, SOCKET s, const SOCKADDR *name, int namelen)
{
    /* The core does all of it: the checks in the documented order, the peer of a datagram
     * socket (and its removal by an AF_UNSPEC name), and the stops it names. */
    return rt_net_connect(s, name, namelen);
}
