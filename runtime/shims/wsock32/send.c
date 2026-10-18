/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-send
 * RT-CASES:  not a socket gets WSAENOTSOCK; stream socket gets WSAENOTCONN; datagram socket with no peer gets WSAENOTCONN; len below zero, or buf NULL with len above zero: WSAEFAULT; shut down for sending: WSAESHUTDOWN; loopback datagram is queued on the receiver and its length comes back; connected datagram socket sends to its peer; len above 65507: WSAEMSGSIZE; success sets last error to 0
 * RT-TRAPS:  core: flags other than 0
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 19, int, send, SOCKET s, LPCVOID buf, int len, int flags)
{
    return rt_net_sendto(s, buf, len, flags, NULL, 0);
}
