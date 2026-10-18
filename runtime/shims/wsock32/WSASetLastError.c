/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-wsasetlasterror
 * RT-CASES:  works before WSAStartup; stores the value in the thread block at +0x68; zero is stored like any other value; a negative value is stored as its 32 bits
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM_ORD(wsock32, 112, void, WSASetLastError, int iError)
{
    rt_set_last_error(ctx, (DWORD)iError);
}
