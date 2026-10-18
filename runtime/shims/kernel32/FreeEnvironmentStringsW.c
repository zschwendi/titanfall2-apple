/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-freeenvironmentstringsw
 * RT-CASES:  frees a block from GetEnvironmentStringsW and returns TRUE; does not change last error; NULL penv returns TRUE and does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/env.h"
#include "rt/win/nls.h"             /* LPWCH */

SHIM(kernel32, BOOL, FreeEnvironmentStringsW, LPWCH penv)
{
    /* The core accepts NULL and returns TRUE (its NULL case is not verified against Windows).
     * A pointer that is not a live block is fatal in the core, which names it; this shim cannot
     * tell such a pointer apart. */
    return rt_env_block_free(penv);
}
