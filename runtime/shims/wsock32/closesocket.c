/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-closesocket
 * RT-CASES:  before WSAStartup fails with WSANOTINITIALISED; closes an open socket, with 0 and last error left alone; closing it again fails with WSAENOTSOCK; 0 and INVALID_SOCKET fail with WSAENOTSOCK; a value with bit 32 set fails with WSAENOTSOCK and leaves the open socket open
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 3, int, closesocket, SOCKET s)
{
    /* The core does all of it (rt_net_close, rt/net.h): it returns 0, or SOCKET_ERROR with
     * WSAENOTSOCK set by the core, and leaves last error alone on success.  The page's other
     * failure codes (WSAENETDOWN, WSAEINPROGRESS, WSAEINTR, WSAEWOULDBLOCK) are not in the core's
     * contract, which documents WSAENOTSOCK only. */
    return rt_net_close(s);
}
