/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-setlasterror
 * RT-CASES:  stores the value in the thread block at +0x68; uses only the low 32 bits of the argument; zero is stored like any other value
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, void, SetLastError, DWORD dwErrCode)
{
    rt_set_last_error(ctx, dwErrCode);
}
