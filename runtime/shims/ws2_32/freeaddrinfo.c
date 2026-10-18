/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ws2tcpip/nf-ws2tcpip-freeaddrinfo
 * RT-CASES:  NULL does nothing and leaves last error alone, also before WSAStartup; a list of two entries is freed and its blocks are no longer live
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM(ws2_32, void, freeaddrinfo, ADDRINFOA *pAddrInfo)
{
    /* The core frees every entry of the list; NULL does nothing.  It sets no last error. */
    rt_net_freeaddrinfo(pAddrInfo);
}
