/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-ioctlsocket
 * RT-CASES:  FIONBIO with 1 and with 0 returns 0 and leaves last error alone; FIONREAD on a new socket stores 0; argp NULL fails with WSAEFAULT; a value that is not a socket fails with WSAENOTSOCK
 * RT-TRAPS:  core: any other command
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 12, int, ioctlsocket, SOCKET s, LONG cmd, ULONG *argp)
{
    /* The core does all of it (rt_net_ioctl, rt/net.h): it returns 0, or SOCKET_ERROR with
     * WSAENOTSOCK or WSAEFAULT set by the core, and leaves last error alone on success.  Any
     * other command stops in the core (before argp is looked at): no guard is added here. */
    return rt_net_ioctl(s, cmd, argp);
}
