/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-getsockname
 * RT-CASES:  bound AF_INET socket gives its family, port, address and sin_zero with namelen 16; bound AF_INET6 socket on :: and port 0 gives namelen 28 and a port that is not 0; unbound socket gets WSAEINVAL; namelen 15 gets WSAEFAULT; not a socket gets WSAENOTSOCK; success sets last error to 0
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 6, int, getsockname, SOCKET s, SOCKADDR *name, int *namelen)
{
    /* The core does all of it: the checks in the documented order, the bound address and
     * port, *namelen, and last error (0 on success). */
    return rt_net_getsockname(s, name, namelen);
}
