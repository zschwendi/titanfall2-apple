/* Device-context records (specification: rt/gdi.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `gdi-dc`.  Tests: tests/core/gdi_dc.c.
 *
 * STATE: none of its own; the RtDc and RtGdiObj records (core/win.c makes them), read and
 * written under the win lock.  Nothing here calls guest code or draws.
 */
#include "win_priv.h"

static HDC dc_make(RtDcKind kind, HWND window) { return rt_win_dc_new(kind, window); }

HDC rt_dc_get(HWND h)
{
    if (h && h != RT_HWND_DESKTOP) {
        RtWindow *w = rt_win_window_ref(h);         /* sets ERROR_INVALID_WINDOW_HANDLE */
        if (!w) return NULL;
        rt_object_unref(&w->obj);
    }
    return dc_make(RT_DC_WINDOW, h == RT_HWND_DESKTOP ? NULL : h);
}

/* Win lock held: the context goes, what is selected into it stays alive and is free again. */
static void unselect_all(RtDc *d)
{
    for (int t = RT_GDI_PEN; t <= RT_GDI_BITMAP; t++) {
        if (!d->sel[t]) continue;
        RtGdiObj *o = (RtGdiObj *)rt_htab_ref(rt_win_table(), d->sel[t], RT_K_GDIOBJ);
        if (o) { if (o->selected) o->selected--; rt_object_unref(&o->obj); }
        d->sel[t] = NULL;
    }
}

/* Close the context `dc` if it is one of `kind_mask` (bits 1 << RtDcKind).  1: closed; 0: not
 * a context; -1: a context of another kind. */
static int dc_close(HDC dc, unsigned kind_mask)
{
    RtDc *d = rt_win_dc_ref(dc);
    if (!d) return 0;
    const DWORD e = rt_thread_get_last_error();
    rt_win_lock();
    const bool live = d->self != NULL, ours = (kind_mask >> d->kind) & 1;
    if (live && ours) { unselect_all(d); d->self = NULL; }
    rt_win_unlock();
    rt_thread_set_last_error(e);
    rt_object_unref(&d->obj);
    if (!live) return 0;
    if (!ours) return -1;
    rt_htab_close(rt_win_table(), dc, RT_K_DC);
    rt_thread_set_last_error(e);
    return 1;
}

INT rt_dc_release(HWND h, HDC dc)
{
    (void)h;                                        /* the window is not compared (review D12) */
    return dc_close(dc, 1u << RT_DC_WINDOW) == 1;
}

HDC rt_dc_create_compatible(HDC dc)
{
    if (dc) {
        RtDc *d = rt_win_dc_ref(dc);
        if (!d) return NULL;
        rt_object_unref(&d->obj);
    }
    return dc_make(RT_DC_MEMORY, NULL);
}

static bool name_is(const WCHAR *s, const char *want)
{
    if (!s) return false;
    size_t i = 0;
    for (; want[i]; i++) {
        WCHAR c = s[i];
        if (c >= u'a' && c <= u'z') c -= 32;
        if (c != (WCHAR)want[i]) return false;
    }
    return s[i] == 0;
}

HDC rt_dc_create_display(const WCHAR *driver, const WCHAR *device)
{
    const bool ok = name_is(driver, "DISPLAY") || name_is(driver, "\\\\.\\DISPLAY1") ||
                    (!driver && name_is(device, "\\\\.\\DISPLAY1"));
    return ok ? dc_make(RT_DC_DISPLAY, NULL) : NULL;
}

BOOL rt_dc_delete(HDC dc)
{
    const int r = dc_close(dc, (1u << RT_DC_DISPLAY) | (1u << RT_DC_MEMORY));
    if (r < 0) RT_WIN_STOP("rt_dc_delete", "a context that GetDC gave out (%p): it is released with ReleaseDC", (void *)dc);
    return r == 1;
}

INT rt_dc_device_cap(HDC dc, INT index)
{
    RtDc *d = rt_win_dc_ref(dc);
    if (!d) { rt_thread_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    rt_object_unref(&d->obj);
    INT v;
    if (!rt_desktop_device_cap(index, &v)) RT_WIN_STOP("rt_dc_device_cap", "index %d", index);
    return v;
}

HGDIOBJ rt_dc_select(HDC dc, HGDIOBJ obj)
{
    RtDc *d = rt_win_dc_ref(dc);
    if (!d) { rt_thread_set_last_error(ERROR_INVALID_HANDLE); return NULL; }
    RtGdiObj *o = obj ? rt_win_gdiobj_ref(obj) : NULL;
    HGDIOBJ old = NULL;
    bool region = false;
    rt_win_lock();
    if (!obj) {                                     /* the stock default back into the slot that left it last */
        int t = -1;
        for (int i = RT_GDI_PEN; i <= RT_GDI_BITMAP; i++)
            if (d->sel[i] && (t < 0 || d->sel_seq[i] > d->sel_seq[t])) t = i;
        if (t >= 0) {
            old = d->sel[t];
            RtGdiObj *was = (RtGdiObj *)rt_htab_ref(rt_win_table(), old, RT_K_GDIOBJ);
            if (was) { if (was->selected) was->selected--; rt_object_unref(&was->obj); }
            d->sel[t] = NULL;
        }
    } else if (o && o->self && o->type == RT_GDI_REGION) {
        region = true;
    } else if (o && o->self && d->sel[o->type] == obj) {
        old = obj;                                  /* selected already: nothing changes */
    } else if (o && o->self && !(o->type == RT_GDI_BITMAP && (d->kind != RT_DC_MEMORY || o->selected))) {
        old = d->sel[o->type];
        if (old) {
            RtGdiObj *was = (RtGdiObj *)rt_htab_ref(rt_win_table(), old, RT_K_GDIOBJ);
            if (was) { if (was->selected) was->selected--; rt_object_unref(&was->obj); }
        } else {
            d->sel_seq[o->type] = ++d->seq;
        }
        d->sel[o->type] = obj;
        o->selected++;
    }
    rt_win_unlock();
    if (o) rt_object_unref(&o->obj);
    rt_object_unref(&d->obj);
    if (region) RT_WIN_STOP("rt_dc_select", "a region (%p): it would become the clip region", (void *)obj);
    return old;
}

bool rt_dc_attr(HDC dc, RtDcAttr which, const ULONG *set, ULONG *old)
{
    RtDc *d = rt_win_dc_ref(dc);
    if (!d) { rt_thread_set_last_error(ERROR_INVALID_HANDLE); return false; }
    if (which < RT_DC_BK_COLOR || which > RT_DC_STRETCH_MODE) { rt_object_unref(&d->obj); rt_fatal("rt_dc_attr: attribute %d", (int)which); }
    if (set && which == RT_DC_BK_MODE && *set != TRANSPARENT && *set != OPAQUE) { rt_object_unref(&d->obj); return false; }
    if (set && which == RT_DC_STRETCH_MODE && (*set < 1 || *set > 4)) {
        rt_object_unref(&d->obj);
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return false;
    }
    rt_win_lock();
    ULONG *slot = which == RT_DC_BK_COLOR ? &d->bk_color : which == RT_DC_TEXT_COLOR ? &d->text_color :
                  which == RT_DC_BK_MODE ? &d->bk_mode : which == RT_DC_TEXT_ALIGN ? &d->text_align : &d->stretch_mode;
    if (old) *old = *slot;
    if (set) *slot = *set;
    rt_win_unlock();
    rt_object_unref(&d->obj);
    return true;
}

BOOL rt_dc_set_point(HDC dc, RtDcPoint which, INT x, INT y, POINT *old)
{
    RtDc *d = rt_win_dc_ref(dc);
    if (!d) { rt_thread_set_last_error(ERROR_INVALID_HANDLE); return FALSE; }
    if (which != RT_DC_VIEWPORT_ORG && which != RT_DC_POSITION) { rt_object_unref(&d->obj); rt_fatal("rt_dc_set_point: point %d", (int)which); }
    rt_win_lock();
    POINT *slot = which == RT_DC_VIEWPORT_ORG ? &d->viewport_org : &d->position;
    if (old) *old = *slot;
    *slot = (POINT){ x, y };
    rt_win_unlock();
    rt_object_unref(&d->obj);
    return TRUE;
}
