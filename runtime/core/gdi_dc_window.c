/* Which window a device context from GetDC looks at (specification: rt/gdi.h, rt_dc_window).
 *
 * Added by unit gate3-run-3 beside the piece `gdi-dc` (core/gdi_dc.c), in a file of its own
 * so that the piece's file is not touched while its unit works on it.  Tests:
 * tests/core/gdi_dc_window.c.
 *
 * STATE: none; it reads one RtDc record under the win lock.
 */
#include "win_priv.h"

int rt_dc_window(HDC dc, HWND *window, POINT *viewport)
{
    const DWORD e = rt_thread_get_last_error();
    RtDc *d = rt_win_dc_ref(dc);
    int r = 0;
    if (d) {
        rt_win_lock();
        if (d->self) {
            r = d->kind == RT_DC_WINDOW ? 1 : -1;
            if (r == 1) { *window = d->window; *viewport = d->viewport_org; }
        }
        rt_win_unlock();
        rt_object_unref(&d->obj);
    }
    rt_thread_set_last_error(e);
    return r;
}
