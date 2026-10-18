/* The piece gdi-dc (rt/gdi.h, core/gdi_dc.c): device-context records.  Expected values: the
 * comments of rt/gdi.h, which name their source for each (Wine gdi32/tests/dc.c, gdiobj.c,
 * bitmap.c and user32/tests/dce.c through reviews C9, D12 to D14, V17, V46 to V49), and
 * rt_desktop_device_cap for the capabilities. */
#include "rt/test.h"
#include "../../core/win_priv.h"

static HWND a_window(void)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = (WNDPROC)0x140001000ull, .lpszClassName = u"DcWin" };
    RtClassKey key = { .name = u"DcWin" };
    if (!rt_wndclass_register(&wc, true)) return NULL;
    rt_win_lock();
    RtWndClass *c = rt_win_class_lookup(&key);
    rt_win_unlock();
    RtWinNew a = { .cls = c, .style = WS_POPUP, .rect = { 0, 0, 10, 10 }, .unicode = true, .thread_id = LD32(T_CTX()->gs_base + 0x48) };
    return rt_win_window_new(&a);
}

TEST(core, gdi_dc)
{
    HWND w = a_window();
    T_REQUIRE(w != NULL);
    rt_thread_set_last_error(0xDEAD);

    T_CASE("GetDC of the screen, the desktop and a window; ReleaseDC once, whatever the window");   /* review D12 */
    HDC s = rt_dc_get(NULL), k = rt_dc_get(RT_HWND_DESKTOP), d = rt_dc_get(w);
    T_TRUE(s && k && d && s != k && k != d && (uintptr_t)d >= 0x40000000);
    T_EQ(rt_dc_release(NULL, d), 1);                /* the wrong window: still 1 */
    T_EQ(rt_dc_release(w, d), 0);                   /* a second release: 0 */
    T_EQ(rt_dc_release(w, NULL), 0);
    T_EQ(rt_dc_release(w, k), 1);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRUE(rt_dc_get((HWND)0x40000FF0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("GetDeviceCaps: the desktop's numbers for every context; a handle that is no context");   /* review C9 */
    HDC m = rt_dc_create_compatible(NULL);
    T_TRUE(m != NULL);
    T_EQ(rt_dc_device_cap(s, HORZRES), 1920);
    T_EQ(rt_dc_device_cap(s, VERTRES), 1080);
    T_EQ(rt_dc_device_cap(s, VREFRESH), 60);        /* the three the game asks (docs/trace/window-calls.md) */
    T_EQ(rt_dc_device_cap(m, BITSPIXEL), 32);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dc_device_cap(d, HORZRES), 0);          /* released above */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_dc_device_cap((HDC)w, HORZRES), 0);     /* a window handle is not a context */
    T_TRAPS(rt_dc_device_cap(s, 38 /* RASTERCAPS */), "rt_dc_device_cap: not implemented: index 38");

    T_CASE("memory and display contexts: made, deleted once");   /* review D13, S26 */
    rt_thread_set_last_error(0xDEAD);
    T_TRUE(rt_dc_create_compatible(s) != NULL);
    T_TRUE(rt_dc_create_compatible((HDC)0x40000FF0) == NULL);
    HDC disp = rt_dc_create_display(u"display", NULL);
    T_TRUE(disp != NULL);
    T_TRUE(rt_dc_create_display(u"\\\\.\\DISPLAY1", u"anything") != NULL);
    T_TRUE(rt_dc_create_display(NULL, u"\\\\.\\display1") != NULL);
    T_TRUE(rt_dc_create_display(NULL, NULL) == NULL);
    T_TRUE(rt_dc_create_display(NULL, u"DISPLAY") == NULL);
    T_TRUE(rt_dc_create_display(u"\\\\.\\DISPLAY1\\Monitor0", NULL) == NULL);
    T_TRUE(rt_dc_create_display(u"WINSPOOL", NULL) == NULL);
    T_EQ(rt_dc_delete(disp), TRUE);
    T_EQ(rt_dc_delete(disp), FALSE);
    T_EQ(rt_dc_delete(NULL), FALSE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRAPS(rt_dc_delete(s), "a context that GetDC gave out");

    T_CASE("the defaults of a new context and the one-value attributes");   /* reviews V17, V46, V48 */
    ULONG v = 77, nv;
    T_TRUE(rt_dc_attr(m, RT_DC_TEXT_COLOR, NULL, &v) && v == 0x000000);
    T_TRUE(rt_dc_attr(m, RT_DC_BK_COLOR, NULL, &v) && v == 0xFFFFFF);
    T_TRUE(rt_dc_attr(m, RT_DC_BK_MODE, NULL, &v) && v == OPAQUE);
    T_TRUE(rt_dc_attr(m, RT_DC_TEXT_ALIGN, NULL, &v) && v == 0);
    T_TRUE(rt_dc_attr(m, RT_DC_STRETCH_MODE, NULL, &v) && v == BLACKONWHITE);
    nv = 0x123456;
    T_TRUE(rt_dc_attr(m, RT_DC_BK_COLOR, &nv, &v) && v == 0xFFFFFF);
    T_TRUE(rt_dc_attr(m, RT_DC_BK_COLOR, NULL, &v) && v == 0x123456);
    nv = TRANSPARENT;
    T_TRUE(rt_dc_attr(m, RT_DC_BK_MODE, &nv, &v) && v == OPAQUE);
    nv = 3;
    T_TRUE(!rt_dc_attr(m, RT_DC_BK_MODE, &nv, NULL));
    T_EQ(T_LAST_ERROR(), 0xDEAD);                   /* a refused mode sets no last error */
    nv = 5;
    T_TRUE(!rt_dc_attr(m, RT_DC_STRETCH_MODE, &nv, NULL));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(rt_dc_attr(m, RT_DC_BK_MODE, NULL, &v) && v == TRANSPARENT);
    T_TRUE(!rt_dc_attr((HDC)0x40000FF0, RT_DC_BK_MODE, NULL, &v));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("the two points");
    POINT p = { 9, 9 };
    T_TRUE(rt_dc_set_point(m, RT_DC_VIEWPORT_ORG, 3, 4, &p) && p.x == 0 && p.y == 0);
    T_TRUE(rt_dc_set_point(m, RT_DC_VIEWPORT_ORG, 5, 6, &p) && p.x == 3 && p.y == 4);
    T_TRUE(rt_dc_set_point(m, RT_DC_POSITION, 7, 8, NULL));
    T_TRUE(rt_dc_set_point(m, RT_DC_POSITION, 0, 0, &p) && p.x == 7 && p.y == 8);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dc_set_point(NULL, RT_DC_POSITION, 0, 0, &p), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("SelectObject: the handle before, NULL the first time; NULL restores the default");   /* reviews D14, V17, V49 */
    HGDIOBJ pen = rt_win_gdiobj_new(RT_GDI_PEN), pen2 = rt_win_gdiobj_new(RT_GDI_PEN), font = rt_win_gdiobj_new(RT_GDI_FONT);
    HGDIOBJ bmp = rt_win_gdiobj_new(RT_GDI_BITMAP), rgn = rt_win_gdiobj_new(RT_GDI_REGION);
    HDC m2 = rt_dc_create_compatible(NULL);
    T_REQUIRE(pen && pen2 && font && bmp && rgn && m2);
    rt_thread_set_last_error(0xDEAD);
    T_TRUE(rt_dc_select(m, pen) == NULL);
    T_TRUE(rt_dc_select(m, pen2) == pen);
    T_TRUE(rt_dc_select(m, pen2) == pen2);
    T_TRUE(rt_dc_select(m, font) == NULL);
    T_TRUE(rt_dc_select(m, NULL) == font);          /* the slot that left its default last */
    T_TRUE(rt_dc_select(m, NULL) == pen2);
    T_TRUE(rt_dc_select(m, NULL) == NULL);          /* everything is default: nothing changes */
    T_TRUE(rt_dc_select(m, pen) == NULL);
    T_TRUE(rt_dc_select(s, bmp) == NULL);           /* a bitmap into a display context */
    T_TRUE(rt_dc_select(m, bmp) == NULL);           /* into a memory context: the first time */
    T_TRUE(rt_dc_select(m, bmp) == bmp);
    T_TRUE(rt_dc_select(m2, bmp) == NULL);          /* selected into another context: refused */
    T_TRUE(rt_dc_select(m2, pen) == NULL);          /* a pen may be in two contexts */
    T_TRUE(rt_dc_select(m, (HGDIOBJ)0x40000FF0) == NULL);
    T_TRUE(rt_dc_select(m, (HGDIOBJ)w) == NULL);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRUE(rt_dc_select((HDC)0x40000FF0, pen) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRAPS(rt_dc_select(m, rgn), "rt_dc_select: not implemented: a region");

    T_CASE("deleting a context frees what was selected into it");
    T_EQ(rt_dc_delete(m), TRUE);
    T_TRUE(rt_dc_select(m2, bmp) == NULL);          /* the first time for m2, and no longer refused: */
    T_TRUE(rt_dc_select(m2, bmp) == bmp);           /* it is in m2 now */
}
