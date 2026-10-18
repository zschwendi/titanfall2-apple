/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-wsaioctl
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM(ws2_32, int, WSAIoctl, SOCKET s, DWORD dwIoControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer, LPVOID lpvOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbBytesReturned, LPVOID lpOverlapped, GUESTFN lpCompletionRoutine)
{
    (void)lpvInBuffer;
    (void)cbInBuffer;
    (void)lpvOutBuffer;
    (void)cbOutBuffer;
    (void)lpcbBytesReturned;
    (void)lpOverlapped;
    (void)lpCompletionRoutine;
    RT_UNIMPLEMENTED(ctx, "WSAIoctl(%llu, 0x%08x)", (unsigned long long)s, dwIoControlCode);
}
