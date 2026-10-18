/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createcompatibledc
 * RT-CASES:  from the screen (NULL) a memory context; from a display context a memory context; a handle that is no context gives NULL
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HDC, CreateCompatibleDC, HDC hdc)
{
    /* The core's memory context (rt/gdi.h, rt_dc_create_compatible); NULL for a handle that is
     * not a context.  The core sets no last error here, and neither does the shim. */
    return rt_dc_create_compatible(hdc);
}
