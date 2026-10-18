/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-enumdisplaymonitors
 * RT-CASES:  no clip gives one call with the handle 0x10001, a NULL HDC, the monitor rectangle and the data; a clip that intersects the monitor still gives one call; a clip that misses the monitor gives no call and TRUE; a callback that returns 0 gives FALSE; success leaves last error alone; the context of a shown window gives one call with that context and the part of the client area on the monitor, in the context's coordinates; the screen's context gives one call with that context and the monitor rectangle; with a context too, a callback that returns 0 gives FALSE
 * RT-TRAPS:  EnumDisplayMonitors with a device context that GetDC did not give out; EnumDisplayMonitors with a device context and a clip; EnumDisplayMonitors with a device context whose viewport origin; EnumDisplayMonitors with the device context of a window that is not visible; EnumDisplayMonitors with the device context of a window that is not the top one; EnumDisplayMonitors with the device context of a window that has a child; EnumDisplayMonitors with the device context of a window whose client area is off the monitor; EnumDisplayMonitors with lpfnEnum 0; EnumDisplayMonitors with a clip that touches or is empty
 */
#include "rt/shim.h"
#include "rt/gdi.h"
#include "rt/window.h"

SHIM(user32, BOOL, EnumDisplayMonitors, HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData)
{
    /* The core has one monitor, the primary, at the origin (rt/window.h, rt_desktop).  A
     * callback of 0 is a stop; a device context is answered in one case (below). */
    const RtDesktop *d = rt_desktop();
    const RECT *m = &d->monitor;
    RECT r;
    BOOL go;

    if (hdc) {
        /* A device context (unit gate3-run-3: the game passes its own window's, from GetDC,
         * with no clip, at materialsystem_dx11+0x154b7).  RT-DOC and the page of
         * MonitorEnumProc, both read 2026-10-10: the function "enumerates all display
         * monitors that intersect the visible region of the device context", and "the callback
         * function receives a handle to a DC for the specific display monitor"; the callback's
         * rectangle "is the intersection of the clipping area of the device context identified
         * by hdcMonitor and the display monitor rectangle. The rectangle coordinates are
         * device-context coordinates."
         *
         * What is answered is the one case where the visible region is known without drawing
         * anything: a context that GetDC gave out, for the screen (its visible region is the
         * screen) or for a window that is shown, is the top one and has no child (its visible
         * region is its client area as far as it lies on the monitor: nothing covers it).
         * Everything else stops.  WHICH HANDLE the callback gets is this runtime's choice:
         * the pages say "a DC for the specific display monitor" and not whether it is a new
         * handle; with one monitor the caller's own context has that monitor's colour format
         * and the clipping area the page describes, so it is passed on.  (The recorded real
         * start, finding 39, shows the same handle value in the callback's GetDeviceGammaRamp
         * as in EnumDisplayMonitors: that is the translation layer's answer, not Windows'.) */
        HWND w = NULL;
        POINT vp = { 0, 0 }, org = { 0, 0 };
        RECT c;

        if (rt_dc_window(hdc, &w, &vp) != 1)
            RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with a device context that GetDC did not give out, or that was released: %p", (void *)hdc);
        if (lprcClip) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with a device context and a clip: %p", (void *)hdc);
        if (vp.x || vp.y) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with a device context whose viewport origin is %d,%d", vp.x, vp.y);
        if (!lpfnEnum) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with lpfnEnum 0");
        if (!w) {
            r = *m;
        } else {
            if (!rt_window_is_visible(w)) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with the device context of a window that is not visible: %p", (void *)w);
            if (rt_window_top(NULL) != w) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with the device context of a window that is not the top one: %p", (void *)w);
            if (rt_window_get(w, GW_CHILD)) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with the device context of a window that has a child: %p", (void *)w);
            if (!rt_window_get_client_rect(w, &c) || !rt_window_client_to_screen(w, &org))
                RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with the device context of a window that is not visible: %p", (void *)w);
            /* The client area on the screen, cut to the monitor, then back to the context's
             * coordinates (their origin is the client area's top left corner). */
            r.left   = (org.x > m->left ? org.x : m->left) - org.x;
            r.top    = (org.y > m->top ? org.y : m->top) - org.y;
            r.right  = (org.x + c.right < m->right ? org.x + c.right : m->right) - org.x;
            r.bottom = (org.y + c.bottom < m->bottom ? org.y + c.bottom : m->bottom) - org.y;
            if (r.left >= r.right || r.top >= r.bottom)
                RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with the device context of a window whose client area is off the monitor: %p", (void *)w);
        }
        go = (BOOL)GUEST_CALL(ctx, lpfnEnum, RT_HMONITOR_PRIMARY, hdc, &r, dwData);
        return go ? TRUE : FALSE;
    }
    if (!lpfnEnum) RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with lpfnEnum 0");
    if (lprcClip) {
        const RECT *c = lprcClip;
        BOOL empty  = c->left >= c->right || c->top >= c->bottom;
        BOOL beyond = c->left > m->right || c->right < m->left || c->top > m->bottom || c->bottom < m->top;
        BOOL inside = c->left < m->right && c->right > m->left && c->top < m->bottom && c->bottom > m->top;
        /* A clip entirely beyond an edge of the monitor misses it.  A clip with area inside the
         * monitor hits it.  A clip that touches an edge or is empty is not settled by the
         * documentation, so it stops the run (it is never guessed). */
        if (!empty && beyond) return TRUE;
        if (empty || !inside)
            RT_UNIMPLEMENTED(ctx, "EnumDisplayMonitors with a clip that touches or is empty: %d,%d,%d,%d",
                             c->left, c->top, c->right, c->bottom);
    }
    /* One call, with the monitor's rectangle in a copy on this stack.  Returning FALSE stops the
     * enumeration, and that makes the function return FALSE; TRUE continues it. */
    r = *m;
    go = (BOOL)GUEST_CALL(ctx, lpfnEnum, RT_HMONITOR_PRIMARY, NULL, &r, dwData);
    return go ? TRUE : FALSE;
}
