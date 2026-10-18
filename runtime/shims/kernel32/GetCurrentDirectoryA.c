/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getcurrentdirectory
 * RT-CASES:  the directory at the start agrees with rt_path_get_cwd_w; a size query, with length 0, gives the length with the terminator and writes nothing; a buffer one byte too small gets an empty string and the length it needs; a buffer that holds the directory gets it with its terminator and the length is returned; success leaves last error alone; a sub-directory set with rt_path_set_cwd_w is read back
 * RT-TRAPS:  a NULL buffer that holds the directory; a current directory of MAX_PATH characters or more; a current directory outside code page 1252
 */
#include "rt/shim.h"
#include "rt/path.h"
#include "rt/locale.h"

SHIM(kernel32, DWORD, GetCurrentDirectoryA, DWORD nBufferLength, LPSTR lpBuffer)
{
    /* The core gives the guest's current directory as WCHARs (rt_path_get_cwd_w); this shim
     * converts it to code page 1252 and then does what the W form does with the buffer.
     * Nothing here fails, so last error is left alone.  The conversion comes first, also for
     * a size query, because the byte count of a directory outside 1252 is not known. */
    WCHAR wide[MAX_PATH];
    char narrow[MAX_PATH];
    DWORD n, i;
    BOOL used = FALSE;
    int k;

    n = rt_path_get_cwd_w(wide, MAX_PATH);
    if (n >= MAX_PATH) RT_UNIMPLEMENTED(ctx, "GetCurrentDirectoryA with a current directory of MAX_PATH characters or more");

    k = rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, (int)(n + 1), narrow, MAX_PATH, NULL, &used);
    if (k == 0 || used) RT_UNIMPLEMENTED(ctx, "GetCurrentDirectoryA with a current directory outside code page 1252");

    if (nBufferLength < n + 1) {
        if (nBufferLength != 0 && lpBuffer != NULL) lpBuffer[0] = 0;
        return n + 1;
    }
    if (lpBuffer == NULL) RT_UNIMPLEMENTED(ctx, "GetCurrentDirectoryA with a NULL buffer that holds the directory");

    for (i = 0; i <= n; i++)
        lpBuffer[i] = narrow[i];
    return n;
}
