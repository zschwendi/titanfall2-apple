/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-getpeername
 * RT-CASES:  a new datagram or stream socket gets WSAENOTCONN, also with NULL name and NULL namelen; after connect to 127.0.0.1 port 4000 the datagram socket reports that address and port with *namelen 16; *namelen 15 gets WSAEFAULT and stays 15; not a socket gets WSAENOTSOCK
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 5, int, getpeername, SOCKET s, SOCKADDR *name, int *namelen)
{
    /* The core does all of it: the checks in the documented order (WSAENOTSOCK, WSAENOTCONN
     * before the pointers, WSAEFAULT), the peer's name, and the last error. */
    return rt_net_getpeername(s, name, namelen);
}
