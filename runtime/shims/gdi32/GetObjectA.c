/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-getobjecta
 * RT-CASES:  pen record of 16 bytes; brush record of 16 bytes; font record of 60 bytes as LOGFONTA; bitmap record of 32 bytes; DIB section record of 104 bytes; NULL buffer returns the size; buffer one byte short returns 0 and is not written; handle that is no object returns 0; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, INT, GetObjectA, HANDLE h, INT c, LPVOID pv)
{
    /* The core's record of the object (rt/gdi.h, rt_gdi_get_object).  It sets no last error,
     * and the shim sets none either. */
    return rt_gdi_get_object(h, c, pv, false);
}
