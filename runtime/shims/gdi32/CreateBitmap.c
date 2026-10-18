/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createbitmap
 * RT-CASES:  creates a bitmap of the given size and format with no pixel storage; formats of 1, 8, 16, 24 and 32 bits per pixel; a width or height of 0 gives a 1 x 1 monochrome bitmap whatever the planes and bits per pixel; success leaves last error alone
 * RT-TRAPS:  core: initial pixels (lpBits not NULL); core: a negative size; core: planes other than 1; core: bits per pixel other than 1, 8, 16, 24 or 32
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HBITMAP, CreateBitmap, INT nWidth, INT nHeight, UINT nPlanes, UINT nBitCount, LPCVOID lpBits)
{
    /* The core does all of it (rt_gdi_create_bitmap, rt/gdi.h): it makes the record and no
     * pixel storage, and sets no last error.  A zero width or height gives 1 x 1 monochrome
     * before the core's stops are looked at.  Its stops (initial pixels, a negative size,
     * planes other than 1, bits per pixel other than 1, 8, 16, 24 or 32) are not guarded here:
     * the core stops the runtime on them. */
    return rt_gdi_create_bitmap(nWidth, nHeight, nPlanes, nBitCount, lpBits);
}
