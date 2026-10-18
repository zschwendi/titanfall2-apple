/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-bind
 * RT-CASES:  AF_INET6 datagram socket bound to :: port 37015 returns 0 and last error 0; port 0 takes a port of 49152 or above; a second socket on the same port gets WSAEADDRINUSE; an address that is not local (192.0.2.1) gets WSAEADDRNOTAVAIL; a second bind on a bound socket gets WSAEINVAL; namelen 27 for AF_INET6 gets WSAEFAULT; name NULL gets WSAEFAULT; a wrong family gets WSAEAFNOSUPPORT; not a socket gets WSAENOTSOCK
 * RT-TRAPS:  core: a port taken while SO_REUSEADDR is set
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 2, int, bind, SOCKET s, const SOCKADDR *name, int namelen)
{
    /* The core does all of it: the checks in the documented order, the port for port 0, last
     * error (0 on success), and the stop on a port taken while SO_REUSEADDR is set. */
    return rt_net_bind(s, name, namelen);
}
