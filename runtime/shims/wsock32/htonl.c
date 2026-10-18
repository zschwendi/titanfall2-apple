/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-htonl
 * RT-CASES:  reverses the four bytes of the value; zero; all ones; the loopback address; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM_ORD(wsock32, 8, ULONG, htonl, ULONG hostlong)
{
    /* No core call: the byte order of the network is fixed, so the four bytes are reversed
     * here, whatever the host's byte order. */
    return hostlong >> 24 | (hostlong >> 8 & 0xFF00) | (hostlong << 8 & 0xFF0000) | hostlong << 24;
}
