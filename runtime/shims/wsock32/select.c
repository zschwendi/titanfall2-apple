/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-select
 * RT-CASES:  three NULL sets and three empty sets get WSAEINVAL; a timeout with a negative member gets WSAEINVAL; a zero timeout does not wait and empties an unready set; a timeout of 20 ms waits and then gives 0 with the set emptied; a datagram socket in writefds is ready and stays in the set; a queued datagram makes readfds ready; a socket listed twice in one set counts once; a socket closed before the call gets WSAENOTSOCK and the sets are unchanged; nfds is ignored; the TIMEVAL is unchanged; success leaves last error alone
 * RT-TRAPS:  core: fd_count above 64
 */
#include "rt/shim.h"
#include "rt/net.h"

/* nfds is ignored by Windows and by the core; the core checks the three sets first, then the
 * timeout, and waits (or not) as the timeout says.  The fd_count above 64 case STOPS in the
 * core (Windows accepts 200): no guard is added here. */
SHIM_ORD(wsock32, 18, int, select, int nfds, FD_SET *readfds, FD_SET *writefds, FD_SET *exceptfds, const TIMEVAL *timeout)
{
    return rt_net_select(nfds, readfds, writefds, exceptfds, timeout);
}
