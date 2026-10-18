/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-getenvironmentstrings
 * RT-CASES:  returns a block of NAME=value strings, each NUL-terminated, ended by a second NUL; holds a variable set with rt_env_set_a; is a snapshot: later changes do not show in it; two calls give two different blocks; success does not change last error
 * RT-TRAPS:  core: a value with a character >= 0x80
 */
#include "rt/shim.h"
#include "rt/env.h"
#include "rt/win/nls.h"             /* LPCH */

SHIM(kernel32, LPCH, GetEnvironmentStrings)
{
    /* A new snapshot of the environment, in guest memory; the caller releases it with
     * FreeEnvironmentStringsA.  When guest memory is exhausted the core gives the Win32 error.
     * A value with a character >= 0x80 STOPS in the core (the A block cannot carry it): no
     * guard here. */
    DWORD err = 0;
    CHAR *block = rt_env_block_a(&err);
    if (!block) return FAIL(ctx, err, NULL);
    return block;
}
