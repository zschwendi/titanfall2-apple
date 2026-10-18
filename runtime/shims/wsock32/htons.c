/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-htons
 * RT-CASES:  swaps the two bytes of a 16-bit value; zero and 0xFFFF come back unchanged; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM_ORD(wsock32, 9, USHORT, htons, USHORT hostshort)
{
    return (USHORT)(hostshort << 8 | hostshort >> 8);
}
