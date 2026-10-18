/* RT-STATUS: trap
 * RT-DOC:    none
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(rttest, BOOL, TrapOnly, HANDLE h)
{
    RT_UNIMPLEMENTED(ctx, "TrapOnly(%p): deliberately not implemented", h);
}
