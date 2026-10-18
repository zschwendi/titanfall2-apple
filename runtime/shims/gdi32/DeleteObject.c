/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-deleteobject
 * RT-CASES:  deletes a brush once and fails the second time; NULL and a window handle fail
 * RT-TRAPS:  core: a brush selected into a memory context
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, BOOL, DeleteObject, HGDIOBJ ho)
{
    /* The core's delete (rt/gdi.h, rt_gdi_delete).  An object selected into a context is a
     * stop of the core (its comment), so the shim adds no guard for it. */
    return rt_gdi_delete(ho);
}
