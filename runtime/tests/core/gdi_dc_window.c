/* rt_dc_window (rt/gdi.h, core/gdi_dc_window.c): which window a context from GetDC looks at.
 * Expected values: the function's own comment in rt/gdi.h (it reports what rt_dc_get stored;
 * no statement about Windows is tested here). */
#include "rt/test.h"
#include "../../core/win_priv.h"

static HWND a_window(void)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = (WNDPROC)0x140001000ull, .lpszClassName = u"DcWindowWin" };
    RtClassKey key = { .name = u"DcWindowWin" };
    if (!rt_wndclass_register(&wc, true)) return NULL;
    rt_win_lock();
    RtWndClass *c = rt_win_class_lookup(&key);
    rt_win_unlock();
    RtWinNew a = { .cls = c, .style = WS_POPUP, .rect = { 0, 0, 10, 10 }, .unicode = true, .thread_id = LD32(T_CTX()->gs_base + 0x48) };
    return rt_win_window_new(&a);
}

TEST(core, gdi_dc_window)
{
    HWND w = a_window(), got = (HWND)(uintptr_t)0x5555;
    POINT vp = { 77, 88 };
    T_REQUIRE(w != NULL);
    rt_thread_set_last_error(0xDEAD);

    T_CASE("a window's context: 1, the window and the viewport origin");
    HDC d = rt_dc_get(w);
    T_REQUIRE(d != NULL);
    T_EQ(rt_dc_window(d, &got, &vp), 1);
    T_TRUE(got == w);
    T_EQ(vp.x, 0);
    T_EQ(vp.y, 0);
    T_TRUE(rt_dc_set_point(d, RT_DC_VIEWPORT_ORG, 3, 4, NULL));
    T_EQ(rt_dc_window(d, &got, &vp), 1);
    T_EQ(vp.x, 3);
    T_EQ(vp.y, 4);

    T_CASE("the screen's context and the desktop's: 1 and NULL");
    HDC s = rt_dc_get(NULL), k = rt_dc_get(RT_HWND_DESKTOP);
    got = w;
    T_EQ(rt_dc_window(s, &got, &vp), 1);
    T_TRUE(got == NULL);
    got = w;
    T_EQ(rt_dc_window(k, &got, &vp), 1);
    T_TRUE(got == NULL);

    T_CASE("a memory context: -1 and nothing written");
    HDC m = rt_dc_create_compatible(NULL);
    T_REQUIRE(m != NULL);
    got = (HWND)(uintptr_t)0x5555; vp = (POINT){ 77, 88 };
    T_EQ(rt_dc_window(m, &got, &vp), -1);
    T_TRUE(got == (HWND)(uintptr_t)0x5555);
    T_EQ(vp.x, 77);

    T_CASE("a released context, NULL and a handle that is no context: 0 and nothing written");
    T_EQ(rt_dc_release(w, d), 1);
    T_EQ(rt_dc_window(d, &got, &vp), 0);
    T_EQ(rt_dc_window(NULL, &got, &vp), 0);
    T_EQ(rt_dc_window((HDC)w, &got, &vp), 0);
    T_TRUE(got == (HWND)(uintptr_t)0x5555);
    T_EQ(vp.y, 88);

    T_CASE("no last error is set by any of it");
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    rt_dc_release(NULL, s);
    rt_dc_release(NULL, k);
    rt_dc_delete(m);
}
