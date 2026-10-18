/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getstartupinfow
 * RT-CASES:  cb is the structure's size; no inherited handles: hStdInput, hStdOutput and hStdError are NULL; lpDesktop and lpTitle are NULL; dwFlags and wShowWindow are 0; writes all 104 bytes and nothing after them; does not change last error
 * RT-TRAPS:  core: NULL lpStartupInfo
 */
#include "rt/shim.h"
#include "rt/env.h"

SHIM(kernel32, void, GetStartupInfoW, LPSTARTUPINFOW lpStartupInfo)
{
    /* The core fills the structure as the process was started with nothing inherited, and is
     * fatal on a NULL buffer.  The documentation names no failure and no last error. */
    rt_env_startup_info(lpStartupInfo);
}
