/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getcurrentdirectory
 * RT-CASES:  returns the length of the current directory, without the terminator; size query and a buffer one WCHAR too small return the length plus one; a buffer one WCHAR too small holds what the core leaves in it; gives a directory that was set, without a trailing backslash; setting .. goes back up; does not change last error
 * RT-TRAPS:  NULL buffer with a nonzero length
 */
#include "rt/shim.h"
#include "rt/path.h"

SHIM(kernel32, DWORD, GetCurrentDirectoryW, DWORD nBufferLength, LPWSTR lpBuffer)
{
    /* The documentation describes the size query (NULL with length 0) and no other NULL
     * buffer; a NULL buffer with a length is not settled, so it is not answered. */
    if (!lpBuffer && nBufferLength)
        RT_UNIMPLEMENTED(ctx, "NULL buffer with a nonzero length (%u)", (unsigned)nBufferLength);
    return rt_path_get_cwd_w(lpBuffer, nBufferLength);
}
