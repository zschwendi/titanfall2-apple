/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  mode 0 succeeds; mode 1 fails with ERROR_INVALID_PARAMETER
 * RT-TRAPS:  mode above 1
 */
#include "rt/shim.h"

SHIM(rttest, BOOL, Partial, DWORD mode)
{
    if (mode == 0) return TRUE;
    if (mode == 1) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    RT_UNIMPLEMENTED(ctx, "Partial with mode above 1: %u", mode);
}
