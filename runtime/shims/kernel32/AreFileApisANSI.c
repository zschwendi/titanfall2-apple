/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-arefileapisansi
 * RT-CASES:  returns TRUE on each of two calls; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, AreFileApisANSI)
{
    /* Every A file function of this runtime reads a name as the ANSI code page, 1252, and
     * nothing can switch them to the OEM code page: SetFileApisToOEM has no shim and no
     * caller. */
    return TRUE;
}
