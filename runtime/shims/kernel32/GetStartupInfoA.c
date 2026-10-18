/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getstartupinfoa
 * RT-CASES:  cb is the structure's size; no inherited handles: hStdInput, hStdOutput and hStdError are NULL; lpReserved, lpDesktop and lpTitle are NULL; dwFlags and wShowWindow are 0; writes all 104 bytes and nothing after them; does not change last error
 * RT-TRAPS:  NULL lpStartupInfo; a start-up string (lpReserved, lpDesktop or lpTitle) not NULL
 */
#include <string.h>

#include "rt/shim.h"
#include "rt/env.h"

SHIM(kernel32, void, GetStartupInfoA, LPSTARTUPINFOA lpStartupInfo)
{
    /* The core fills the W structure, whose three strings are NULL.  The A structure has the
     * same layout, so every member that is not a string is copied; the 104 bytes are zeroed
     * first, so the padding is zero as in the core's output.  A NULL buffer is not a case the
     * shim handles: the core is fatal on one, and the shim stops before it. */
    STARTUPINFOW w;

    if (!lpStartupInfo) RT_UNIMPLEMENTED(ctx, "GetStartupInfoA with NULL lpStartupInfo");
    rt_env_startup_info(&w);
    if (w.lpReserved || w.lpDesktop || w.lpTitle)
        RT_UNIMPLEMENTED(ctx, "GetStartupInfoA with a start-up string (lpReserved, lpDesktop or lpTitle) not NULL: %p %p %p",
                         (void *)w.lpReserved, (void *)w.lpDesktop, (void *)w.lpTitle);

    memset(lpStartupInfo, 0, sizeof *lpStartupInfo);
    lpStartupInfo->cb = w.cb;
    lpStartupInfo->dwX = w.dwX;
    lpStartupInfo->dwY = w.dwY;
    lpStartupInfo->dwXSize = w.dwXSize;
    lpStartupInfo->dwYSize = w.dwYSize;
    lpStartupInfo->dwXCountChars = w.dwXCountChars;
    lpStartupInfo->dwYCountChars = w.dwYCountChars;
    lpStartupInfo->dwFillAttribute = w.dwFillAttribute;
    lpStartupInfo->dwFlags = w.dwFlags;
    lpStartupInfo->wShowWindow = w.wShowWindow;
    lpStartupInfo->cbReserved2 = w.cbReserved2;
    lpStartupInfo->lpReserved2 = w.lpReserved2;
    lpStartupInfo->hStdInput = w.hStdInput;
    lpStartupInfo->hStdOutput = w.hStdOutput;
    lpStartupInfo->hStdError = w.hStdError;
}
