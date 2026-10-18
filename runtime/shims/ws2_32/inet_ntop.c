/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ws2tcpip/nf-ws2tcpip-inet_ntop
 * RT-CASES:  AF_INET bytes C0 A8 00 01 give 192.168.0.1 and the buffer is returned; AF_INET6 ::1 gives ::1 and 2001:0db8:0:0:0:0:0:1 gives 2001:db8::1; a buffer one byte too small gives NULL, STATUS_INVALID_PARAMETER and an untouched buffer; a zero size or a NULL buffer gives NULL and STATUS_INVALID_PARAMETER; a NULL address gives NULL and STATUS_INVALID_PARAMETER; family 0 and family 99 give NULL and WSAEAFNOSUPPORT; success leaves last error alone; AF_INET6 a zero group on its own is written 0, not :: (1111:2222:3333:4444:5555:6666:0:8888 and 2001:db8:0:1:1:1:1:1); AF_INET6 first 96 bits zero: dotted when group 6 is not 0 (::0.1.0.0, ::13.1.68.3, ::204.152.189.116), hex when it is 0 (::ffff, ::2, ::); AF_INET6 ::ffff:a.b.c.d is dotted only when group 6 is not 0 (::ffff:13.1.68.3, ::ffff:0.1.0.0), hex otherwise (::ffff:0:4403, ::ffff:0:0); AF_INET6 ::ffff:0:a.b.c.d is dotted when group 6 is not 0 (::ffff:0:13.1.68.3), and ISATAP addresses (group 5 5efe, group 4 0 or 0200) are dotted, group 4 0100 is hex (::100:5efe:8190:3426)
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM(ws2_32, PCSTR, inet_ntop, INT Family, LPCVOID pAddr, PSTR pStringBuf, SIZE_T StringBufSize)
{
    /* The core checks the family, the address, the buffer and its size, sets the last error when it fails
     * and leaves it alone on success, so the shim sets nothing.  The core writes every address form the
     * Windows text is known for, so the shim adds no guard. */
    return rt_net_ntop(Family, pAddr, pStringBuf, StringBufSize);
}
