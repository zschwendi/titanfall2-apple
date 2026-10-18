/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ioapiset/nf-ioapiset-deviceiocontrol
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, DeviceIoControl, HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
    (void)lpInBuffer;
    (void)lpOutBuffer;
    (void)lpBytesReturned;
    (void)lpOverlapped;
    RT_UNIMPLEMENTED(ctx, "DeviceIoControl(handle %p, code 0x%08X, %u bytes in, %u bytes out)", hDevice, (unsigned)dwIoControlCode, (unsigned)nInBufferSize, (unsigned)nOutBufferSize);
}
