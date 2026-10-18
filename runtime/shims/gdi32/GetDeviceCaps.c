/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-getdevicecaps
 * RT-CASES:  the desktop's sizes, depth, pixels per inch and refresh rate for a display context; the same for a memory context; a handle that is no context; does not change last error on success
 * RT-TRAPS:  core: an index rt_desktop_device_cap does not answer
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, INT, GetDeviceCaps, HDC hdc, INT index)
{
    /* The core's answer (rt/gdi.h, rt_dc_device_cap).  An index the desktop does not answer
     * is a stop of the core (its comment), so the shim adds no guard for it. */
    return rt_dc_device_cap(hdc, index);
}
