/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createdibsection
 * RT-CASES:  creates a DIB section of zero-filled guest memory of the row size and height given; a top-down section reports its absolute height; a 16-bit BI_RGB section reports BI_BITFIELDS and the 5-5-5 masks; a 24-bit row is rounded up to 4 bytes; NULL pbmi, a width of 0 and a biSize of 39 give NULL with ERROR_INVALID_PARAMETER; success leaves last error alone; deleting a DIB section gives TRUE and the handle is no object
 * RT-TRAPS:  core: section handle (hSection not NULL); core: biSize of 12 (BITMAPCOREHEADER); core: usage other than DIB_RGB_COLORS; core: biPlanes other than 1; core: bits per pixel other than 8, 16, 24 or 32; core: compression other than BI_RGB and BI_BITFIELDS; core: more than 256 MB
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HBITMAP, CreateDIBSection, HDC hdc, const BITMAPINFO *pbmi, UINT usage, LPVOID *ppvBits, HANDLE hSection, DWORD offset)
{
    /* The core does all of it (rt_gdi_create_dib_section, rt/gdi.h): the argument checks that
     * give NULL with ERROR_INVALID_PARAMETER, the record, and the zero-filled memory whose
     * address it writes to *ppvBits.  It sets last error itself on failure and leaves it alone
     * on success.  Its stops (a section handle, a biSize of 12, usage, planes, bits per pixel,
     * compression, more than 256 MB) are not guarded here: the core stops the runtime on them. */
    return rt_gdi_create_dib_section(hdc, pbmi, usage, ppvBits, hSection, offset);
}
