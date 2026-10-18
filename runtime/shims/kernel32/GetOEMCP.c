/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getoemcp
 * RT-CASES:  returns the OEM code page of the en-US machine (437); does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, UINT, GetOEMCP)
{
    /* The core's OEM code page: 437, read from the machine description. */
    return rt_cp_oemcp();
}
