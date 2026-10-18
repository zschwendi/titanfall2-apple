/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ws2tcpip/nf-ws2tcpip-inet_pton
 * RT-CASES:  AF_INET dotted-decimal text gives 1 and four bytes in network order; AF_INET6 text gives 1 and sixteen bytes in network order; text that is not an address of the family gives 0; another family gives -1 and WSAEAFNOSUPPORT; success leaves last error alone; NULL text or buffer gives -1 and WSAEFAULT
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM(ws2_32, INT, inet_pton, INT Family, PCSTR pszAddrString, PVOID pAddrBuf)
{
    /* The core checks the family and the pointers, converts, and sets WSAEAFNOSUPPORT or
     * WSAEFAULT itself when it fails; it leaves last error alone on success.  The shim sets
     * nothing.  The core has no stop for this function: a leading zero or a scope gives 0. */
    return rt_net_pton(Family, pszAddrString, pAddrBuf);
}
