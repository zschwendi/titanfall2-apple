/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-sethandlecount
 * RT-CASES:  returns the number of file handles requested; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, UINT, SetHandleCount, UINT uNumber)
{
    /* For Windows Win32 systems this API has no effect; the return value is the number
     * requested. No log line: there is nothing to say. */
    return uNumber;
}
