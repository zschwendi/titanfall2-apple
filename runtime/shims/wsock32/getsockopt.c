/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-getsockopt
 * RT-CASES:  defaults of a new socket: BOOLs 0, SO_LINGER {0, 0} on a stream socket, SO_SNDBUF and SO_RCVBUF 65536, SO_ERROR 0, SO_TYPE the socket's type; one-byte options: TCP_NODELAY and IPV6_V6ONLY give one byte and leave the other three bytes; a short optlen for a four-byte option, a NULL optval or optlen, or *optlen 0 gives WSAEFAULT; SO_LINGER on a datagram socket gives WSAENOPROTOOPT; not a socket gives WSAENOTSOCK; a call that succeeds sets last error to 0
 * RT-TRAPS:  core: any other level or option; core: a negative optlen; core: IPV6_V6ONLY on an AF_INET socket before it was set
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 7, int, getsockopt, SOCKET s, int level, int optname, char *optval, int *optlen)
{
    /* The core checks the arguments in the documented order, stores the value and the size,
     * and sets last error itself (rt_net_getsockopt, rt/net.h).  The shim adds no guard: the
     * core's stops stay stops. */
    return rt_net_getsockopt(s, level, optname, optval, optlen);
}
