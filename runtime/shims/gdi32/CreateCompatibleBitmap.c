/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createcompatiblebitmap
 * RT-CASES:  a display context gives a bitmap of the screen's format (32 bits); a memory context gives a monochrome bitmap, or the format of the bitmap selected into it; a width or height of 0 gives a 1 x 1 monochrome bitmap; a handle that is no context gives NULL; success and failure leave last error alone
 * RT-TRAPS:  core: a negative size
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HBITMAP, CreateCompatibleBitmap, HDC hdc, INT cx, INT cy)
{
    /* The core does all of it (rt_gdi_create_compatible_bitmap, rt/gdi.h): the format comes
     * from the context, NULL is returned for a handle that is not a context, and no last error
     * is set.  A negative size stops the runtime in the core; the shim adds no guard for it. */
    return rt_gdi_create_compatible_bitmap(hdc, cx, cy);
}
