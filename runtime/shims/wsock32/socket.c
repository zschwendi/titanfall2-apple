/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-socket
 * RT-CASES:  before WSAStartup gives INVALID_SOCKET and WSANOTINITIALISED; AF_INET6 datagram and AF_INET datagram give two new sockets and set last error to 0; socket type 0 with IPPROTO_UDP succeeds and sets last error to 0; address family other than AF_UNSPEC, AF_INET or AF_INET6 gives WSAEAFNOSUPPORT; socket type other than 0, SOCK_STREAM or SOCK_DGRAM gives WSAESOCKTNOSUPPORT; protocol that does not go with the type gives WSAEPROTONOSUPPORT; AF_UNSPEC with protocol 0 gives WSAEINVAL; AF_UNSPEC with IPPROTO_UDP gives a socket
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 23, SOCKET, socket, int af, int type, int protocol)
{
    /* The core checks the arguments in the documented order and sets last error on failure. */
    return rt_net_socket(af, type, protocol);
}
