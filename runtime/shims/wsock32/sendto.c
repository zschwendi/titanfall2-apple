/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-sendto
 * RT-CASES:  5 bytes to a bound loopback socket: 5, and the receiver gets them; 5 bytes to a loopback port nobody holds: 5; to a non-loopback address: WSAENETUNREACH and a log line; to the unspecified address: WSAEADDRNOTAVAIL; 16 bytes from an unbound socket to port 0 on loopback: 16; a send that succeeds leaves last error 0; a datagram longer than 65507 bytes: WSAEMSGSIZE; a stream socket that is not connected: WSAENOTCONN; not a socket: WSAENOTSOCK
 * RT-TRAPS:  core: flags other than 0; core: family not the socket's; core: to on a connected socket; core: IPv4-mapped destination on an IPV6_V6ONLY socket
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 20, int, sendto, SOCKET s, LPCVOID buf, int len, int flags, const SOCKADDR *to, int tolen)
{
    return rt_net_sendto(s, buf, len, flags, to, tolen);
}
