/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-inet_addr
 * RT-CASES:  dotted quad gives the network-order address; three-part, two-part and one-part forms; hex and octal parts; NULL gives INADDR_NONE and WSAEFAULT; empty string gives INADDR_NONE; text that is not an address gives INADDR_NONE; five parts give INADDR_NONE; a part above 255 gives INADDR_NONE; 255.255.255.255 gives INADDR_NONE; one space gives 0; white space after a valid address ends the text; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 10, ULONG, inet_addr, const char *cp)
{
    /* The core does the whole conversion: INADDR_NONE for what Windows refuses, WSAEFAULT for
     * NULL, and white space after a valid address ends the text (Wine ws2_32/tests/protocol.c,
     * test_inet_addr; review Z8).  Every other text leaves last error alone.  No guard here. */
    return rt_net_inet_addr(cp);
}
