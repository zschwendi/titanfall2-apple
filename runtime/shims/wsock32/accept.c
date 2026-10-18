/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-accept
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 1, SOCKET, accept, SOCKET s, SOCKADDR *addr, int *addrlen)
{
    (void)addr;
    (void)addrlen;
    RT_UNIMPLEMENTED(ctx, "accept(%llu)", (unsigned long long)s);
}
