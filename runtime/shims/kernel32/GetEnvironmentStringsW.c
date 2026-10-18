/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-getenvironmentstringsw
 * RT-CASES:  returns a block of NAME=value strings, each NUL-terminated, ended by a second NUL; holds SystemRoot; is a snapshot: later changes do not show in it; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/env.h"
#include "rt/win/nls.h"             /* LPWCH */

SHIM(kernel32, LPWCH, GetEnvironmentStringsW)
{
    /* A new snapshot of the environment, in guest memory; the caller releases it with
     * FreeEnvironmentStringsW.  When guest memory is exhausted the core gives the Win32 error. */
    DWORD err = 0;
    WCHAR *block = rt_env_block_w(&err);
    if (!block) return FAIL(ctx, err, NULL);
    return block;
}
