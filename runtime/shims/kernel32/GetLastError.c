/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
 * RT-CASES:  returns the value SetLastError stored; reads the thread block field at +0x68; leaves the value in place
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, DWORD, GetLastError)
{
    return rt_get_last_error(ctx);
}
