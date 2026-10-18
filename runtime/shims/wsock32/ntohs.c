/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-ntohs
 * RT-CASES:  swaps the two bytes of a network-order value; zero is unchanged; 0xFFFF is unchanged; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM_ORD(wsock32, 15, USHORT, ntohs, USHORT netshort)
{
    /* Swap the two bytes: the network order is big-endian, the host (arm64) is little-endian.
     * No core function is needed: the answer is fixed, and ntohs needs no WSAStartup. */
    return (USHORT)(netshort << 8 | netshort >> 8);
}
