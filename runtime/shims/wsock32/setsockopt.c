/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-setsockopt
 * RT-CASES:  the engine's calls on a UDP socket read back as set; SO_KEEPALIVE, SO_LINGER and TCP_NODELAY on a TCP socket read back as set; TCP_NODELAY on a datagram socket gives WSAENOPROTOOPT; an optlen too small gives WSAEFAULT; SO_REUSEADDR with optlen 1 gives 0 and reads back 1; TCP_NODELAY keeps only the first byte of its value; IPV6_V6ONLY 0 on an AF_INET datagram socket gives 0; not a socket gives WSAENOTSOCK; a call that succeeds sets last error to 0
 * RT-TRAPS:  core: any other level or option; core: SO_LINGER on a datagram socket; core: a negative optlen; core: a negative buffer size; core: IPV6_V6ONLY after bind; core: optlen 0 for SO_REUSEADDR; core: optlen 0 for SO_BROADCAST; core: setting SO_ERROR
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 21, int, setsockopt, SOCKET s, int level, int optname, LPCVOID optval, int optlen)
{
    /* The core checks the arguments, sets last error and stores the option (rt_net_setsockopt,
     * rt/net.h).  The shim adds no guard: the core's stops stay stops. */
    return rt_net_setsockopt(s, level, optname, optval, optlen);
}
