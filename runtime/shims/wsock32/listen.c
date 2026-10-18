/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-listen
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 13, int, listen, SOCKET s, int backlog)
{
    RT_UNIMPLEMENTED(ctx, "listen(%llu, %d)", (unsigned long long)s, backlog);
}
