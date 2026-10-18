/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo
 * RT-CASES:  before WSAStartup gives WSANOTINITIALISED; example.invalid gives WSAHOST_NOT_FOUND, NULL result and a net log line; localhost with the engine's hints gives two AF_INET6 entries, ::1 and ::ffff:127.0.0.1 in either order, and last error 0; 127.0.0.1 with service 80 and NULL hints gives one AF_INET entry on port 80 and last error 0; empty node with service 0 succeeds; [::1] with NULL hints gives one AF_INET6 entry ::1; both names NULL give WSAHOST_NOT_FOUND; ai_family 99 gives WSAEAFNOSUPPORT
 * RT-TRAPS:  core: a service that is not a number; core: result NULL; core: a hints flag outside the known set; core: AI_CANONNAME; core: AI_ADDRCONFIG with an address or a local name; core: text after [addr] other than :digits
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM(ws2_32, INT, getaddrinfo, PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA *pHints, ADDRINFOA **ppResult)
{
    /* The core returns the code and sets last error to it, or returns 0 with the list; it STOPS on
     * the cases its comment names, and the shim adds no guard for them. */
    return rt_net_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);
}
