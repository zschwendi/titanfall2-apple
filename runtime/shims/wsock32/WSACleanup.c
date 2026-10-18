/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-wsacleanup
 * RT-CASES:  not started returns SOCKET_ERROR with WSANOTINITIALISED; two calls after two starts return 0 and set last error to 0; a third call fails again with WSANOTINITIALISED
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 116, int, WSACleanup)
{
    /* The core's start count and its SOCKET_ERROR / WSANOTINITIALISED failure, which it sets
     * as last error itself; on success it sets last error to 0 (rt/net.h, LAST ERROR). */
    return rt_net_cleanup();
}
