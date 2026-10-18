/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-deletedc
 * RT-CASES:  deletes a memory context once and fails the second time; a handle that is no context fails
 * RT-TRAPS:  core: DeleteDC of a context that GetDC gave out
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, BOOL, DeleteDC, HDC hdc)
{
    /* The core's delete (rt/gdi.h, rt_dc_delete).  Deleting a context that GetDC gave out is
     * a stop of the core (its comment), so the shim adds no guard for it. */
    return rt_dc_delete(hdc);
}
