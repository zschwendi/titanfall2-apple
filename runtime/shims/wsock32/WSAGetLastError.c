/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-wsagetlasterror
 * RT-CASES:  returns WSAEWOULDBLOCK the thread stored; returns 0xC0000005 as the int -1073741819; two calls in a row give the same value; reads the thread block field at +0x68
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM_ORD(wsock32, 111, int, WSAGetLastError)
{
    return (int)rt_get_last_error(ctx);
}
