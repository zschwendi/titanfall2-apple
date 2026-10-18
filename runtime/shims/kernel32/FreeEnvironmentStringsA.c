/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-freeenvironmentstringsa
 * RT-CASES:  frees a block from GetEnvironmentStringsA and returns TRUE; does not change last error; NULL penv returns TRUE and does not change last error
 * RT-TRAPS:  core: freeing a pointer that is not a live block
 */
#include "rt/shim.h"
#include "rt/env.h"
#include "rt/win/nls.h"             /* LPCH */

SHIM(kernel32, BOOL, FreeEnvironmentStringsA, LPCH penv)
{
    /* The core accepts NULL and returns TRUE.  A pointer that is not a live block STOPS in the
     * core, which names it: this shim cannot tell such a pointer apart, so it adds no guard. */
    return rt_env_block_free(penv);
}
