/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-selectobject
 * RT-CASES:  selects a brush into a memory context, the first returns NULL and the next the first brush; a pen and a font have slots of their own; a bitmap into a display context is NULL; a handle that is no object is NULL; a handle that is not a context gives NULL with ERROR_INVALID_HANDLE; NULL restores the default into the slot that most recently left it, and reverse NULLs end where they began; selecting what is selected returns it and changes nothing; a bitmap selected into another context is NULL; a pen or font may be selected into two contexts; success leaves last error alone
 * RT-TRAPS:  core: a region
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HGDIOBJ, SelectObject, HDC hdc, HGDIOBJ h)
{
    /* The core's select (rt/gdi.h, rt_dc_select).  It sets ERROR_INVALID_HANDLE itself for a
     * handle that is not a context, and a region is a stop of the core (its comment), so the
     * shim adds no guard for either. */
    return rt_dc_select(hdc, h);
}
