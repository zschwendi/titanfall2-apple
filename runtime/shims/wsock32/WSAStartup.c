/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-wsastartup
 * RT-CASES:  requested 2.2 succeeds and fills the structure except lpVendorInfo; requested 1.1 keeps wVersion 1.1, reports high version 2.2 and the 1.x limits; requested 1.2 to 1.255 gives wVersion 1.1; a version above 2.2 gives wVersion 2.2; NULL output pointer gives WSAEFAULT; major version 0 gives WSAVERNOTSUPPORTED and still fills the structure; last error is left alone on every return
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 115, int, WSAStartup, WORD wVersionRequested, LPWSADATA lpWSAData)
{
    return rt_net_startup(wVersionRequested, lpWSAData);
}
