#include <string.h>

#include "rt/test.h"
#include "rt/window.h"

/* The window procedure of the test's windows is a guest function that records every message
 * it is sent (window, message, wParam) and answers 1 to WM_NCCREATE only, as a procedure must
 * for CreateWindow to go on.  It does not pass anything on, so a window is created untitled.
 * Its WM_NCCALCSIZE takes proc_frame off the rectangle, which is the frame the core would take
 * for the window being created (the test sets it for that one window), so CreateWindow's check
 * of the client area passes. */
#define NSEEN 64
static HWND     seen_h[NSEEN];
static UINT     seen_msg[NSEEN];
static WPARAM   seen_wp[NSEEN];
static unsigned nseen;
static RECT     proc_frame;

GUEST_FN(proc)
{
    if (nseen < NSEEN) {
        seen_h[nseen] = (HWND)(uintptr_t)ctx->r[1];
        seen_msg[nseen] = (UINT)ctx->r[2];
        seen_wp[nseen] = (WPARAM)ctx->r[8];
        nseen++;
    }
    if ((UINT)ctx->r[2] == WM_NCCALCSIZE) {
        RECT *rc = (RECT *)(uintptr_t)ctx->r[9];     /* the first RECT of NCCALCSIZE_PARAMS too */
        if (rc) {
            rc->left += proc_frame.left;
            rc->top += proc_frame.top;
            rc->right -= proc_frame.right;
            rc->bottom -= proc_frame.bottom;
        }
        ctx->r[0] = 0;
        GUEST_RET(ctx);
    }
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

static bool got(HWND h, UINT msg, WPARAM wp)
{
    for (unsigned i = 0; i < nseen; i++)
        if (seen_h[i] == h && seen_msg[i] == msg && seen_wp[i] == wp) return true;
    return false;
}

/* A window of the class "TW" (wide) or "TA" (ANSI), registered once by the test. */
static HWND make_w(DWORD style)
{
    RtWindowCreate w = { .style = style, .cls = RT_TEXT_W(u"TW"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &w);
}

static HWND make_a(DWORD style)
{
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_A("TA"), .title = RT_TEXT_A("Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = false };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, DefWindowProcW)
{
    WNDCLASSEXW cw = { .cbSize = sizeof cw, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"TW", .cbWndExtra = 16 };
    WNDCLASSEXA ca = { .cbSize = sizeof ca, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = "TA", .cbWndExtra = 16 };
    rt_wndclass_register(&cw, true);
    rt_wndclass_register((const WNDCLASSEXW *)&ca, false);

    HWND h = make_w(WS_POPUP);
    HWND h2, h3, h5, h6, h7, ha;
    RECT r, insets;
    WINDOWPOS wp;
    NCCALCSIZE_PARAMS p;
    WCHAR buf[16];
    T_TRUE(h != NULL);

    T_CASE("WM_NCCREATE stores the title and returns 1");
    {
        CREATESTRUCTW cs = { .lpszName = u"Stored", .lpszClass = u"TW", .cx = 300, .cy = 200, .x = 10, .y = 20, .style = (LONG)WS_POPUP };
        T_EQ(CALL(user32, DefWindowProcW, h, WM_NCCREATE, 0, (LPARAM)&cs), 1);
        T_EQ(CALL(user32, DefWindowProcW, h, WM_GETTEXTLENGTH, 0, 0), 6);
    }

    T_CASE("WM_NCCALCSIZE shrinks the client rectangle by the frame and returns 0");
    T_TRUE(rt_window_frame(WS_POPUP | WS_CAPTION, 0, FALSE, &insets));
    proc_frame = insets;
    h2 = make_w(WS_POPUP | WS_CAPTION);
    proc_frame = (RECT){ 0, 0, 0, 0 };
    T_TRUE(h2 != NULL);
    r = (RECT){ 0, 0, 300, 200 };
    T_EQ(CALL(user32, DefWindowProcW, h2, WM_NCCALCSIZE, FALSE, (LPARAM)&r), 0);
    T_EQ(r.left, insets.left);
    T_EQ(r.top, insets.top);
    T_EQ(r.right, 300 - insets.right);
    T_EQ(r.bottom, 200 - insets.bottom);
    /* wParam TRUE: only the first rectangle of the NCCALCSIZE_PARAMS changes */
    p.rgrc[0] = (RECT){ 0, 0, 300, 200 };
    p.rgrc[1] = (RECT){ 1, 2, 3, 4 };
    p.rgrc[2] = (RECT){ 5, 6, 7, 8 };
    wp.x = 77;
    p.lppos = &wp;
    T_EQ(CALL(user32, DefWindowProcW, h2, WM_NCCALCSIZE, TRUE, (LPARAM)&p), 0);
    T_EQ(p.rgrc[0].left, insets.left);
    T_EQ(p.rgrc[0].top, insets.top);
    T_EQ(p.rgrc[0].right, 300 - insets.right);
    T_EQ(p.rgrc[0].bottom, 200 - insets.bottom);
    T_TRUE(memcmp(&p.rgrc[1], &(RECT){ 1, 2, 3, 4 }, sizeof(RECT)) == 0);
    T_TRUE(memcmp(&p.rgrc[2], &(RECT){ 5, 6, 7, 8 }, sizeof(RECT)) == 0);
    T_EQ(wp.x, 77);

    T_CASE("WM_NCCALCSIZE with a NULL lParam returns 0");
    T_EQ(CALL(user32, DefWindowProcW, h2, WM_NCCALCSIZE, FALSE, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h2, WM_NCCALCSIZE, TRUE, 0), 0);

    T_CASE("WM_CREATE WM_DESTROY WM_NCDESTROY WM_NULL WM_MOVE WM_SIZE WM_PARENTNOTIFY return 0");
    T_EQ(CALL(user32, DefWindowProcW, h, WM_CREATE, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_DESTROY, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_NCDESTROY, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_NULL, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_MOVE, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_SIZE, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_PARENTNOTIFY, WM_CREATE, 0), 0);

    T_CASE("WM_CLOSE destroys the window and returns 0");
    h3 = make_w(WS_POPUP);
    T_TRUE(h3 != NULL);
    T_EQ(CALL(user32, DefWindowProcW, h3, WM_SETTEXT, 0, (LPARAM)u"Abc"), 1);
    nseen = 0;
    T_EQ(CALL(user32, DefWindowProcW, h3, WM_CLOSE, 0, 0), 0);
    T_TRUE(got(h3, WM_DESTROY, 0));
    T_EQ(CALL(user32, DefWindowProcW, h3, WM_GETTEXTLENGTH, 0, 0), 0);   /* destroyed: an invalid window */

    T_CASE("WM_SETTEXT WM_GETTEXT WM_GETTEXTLENGTH store and copy the text");
    T_EQ(CALL(user32, DefWindowProcW, h, WM_SETTEXT, 0, (LPARAM)u"Hello"), 1);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_GETTEXTLENGTH, 0, 0), 5);
    for (unsigned i = 0; i < 16; i++) buf[i] = L'x';
    T_EQ(CALL(user32, DefWindowProcW, h, WM_GETTEXT, 16, (LPARAM)buf), 5);   /* count in characters */
    T_TRUE(memcmp(buf, u"Hello", 6 * sizeof(WCHAR)) == 0);             /* five characters and the NUL */
    T_TRUE(buf[6] == L'x' && buf[15] == L'x');                           /* nothing written past the NUL */

    T_CASE("WM_WINDOWPOSCHANGING returns 0");
    memset(&wp, 0, sizeof wp);
    wp.hwnd = h;
    wp.x = 10; wp.y = 20; wp.cx = 300; wp.cy = 200;
    wp.flags = SWP_NOSIZE;
    T_EQ(CALL(user32, DefWindowProcW, h, WM_WINDOWPOSCHANGING, 0, (LPARAM)&wp), 0);

    T_CASE("WM_WINDOWPOSCHANGED sends WM_MOVE and WM_SIZE as its flags say");
    h5 = make_w(WS_POPUP);
    T_TRUE(h5 != NULL);
    memset(&wp, 0, sizeof wp);
    wp.hwnd = h5; wp.x = 10; wp.y = 20; wp.cx = 300; wp.cy = 200;
    nseen = 0;
    T_EQ(CALL(user32, DefWindowProcW, h5, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp), 0);
    T_EQ(nseen, 2); T_EQ(seen_msg[0], WM_MOVE); T_EQ(seen_msg[1], WM_SIZE);
    nseen = 0; wp.flags = SWP_NOCLIENTMOVE;
    T_EQ(CALL(user32, DefWindowProcW, h5, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp), 0);
    T_EQ(nseen, 1); T_EQ(seen_msg[0], WM_SIZE);
    nseen = 0; wp.flags = SWP_NOCLIENTSIZE;
    T_EQ(CALL(user32, DefWindowProcW, h5, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp), 0);
    T_EQ(nseen, 1); T_EQ(seen_msg[0], WM_MOVE);
    nseen = 0; wp.flags = SWP_NOCLIENTMOVE | SWP_NOCLIENTSIZE;
    T_EQ(CALL(user32, DefWindowProcW, h5, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp), 0);
    T_EQ(nseen, 0);

    T_CASE("WM_ACTIVATE moves the focus and returns 0");
    h6 = make_w(WS_POPUP);
    h7 = make_w(WS_POPUP);
    T_TRUE(h6 != NULL && h7 != NULL);
    nseen = 0;
    T_EQ(CALL(user32, DefWindowProcW, h6, WM_ACTIVATE, WA_ACTIVE, 0), 0);
    T_TRUE(got(h6, WM_SETFOCUS, 0));                      /* no window had the focus before */
    nseen = 0;
    T_EQ(CALL(user32, DefWindowProcW, h7, WM_ACTIVATE, WA_ACTIVE, 0), 0);
    T_TRUE(got(h6, WM_KILLFOCUS, (WPARAM)h7));
    T_TRUE(got(h7, WM_SETFOCUS, (WPARAM)h6));

    T_CASE("WM_NCACTIVATE returns TRUE");
    T_EQ(CALL(user32, DefWindowProcW, h, WM_NCACTIVATE, TRUE, 0), TRUE);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_NCACTIVATE, FALSE, 0), TRUE);

    T_CASE("WM_SHOWWINDOW WM_ACTIVATEAPP WM_SETFOCUS WM_KILLFOCUS return 0");
    T_EQ(CALL(user32, DefWindowProcW, h, WM_SHOWWINDOW, TRUE, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_ACTIVATEAPP, TRUE, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_SETFOCUS, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_KILLFOCUS, 0, 0), 0);

    T_CASE("user, application and registered messages return 0");
    T_EQ(CALL(user32, DefWindowProcW, h, 0x0400, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_USER + 1, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, 0xC123, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_APP, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, 0xFFFF, 0, 0), 0);

    T_CASE("an invalid window returns 0");
    T_EQ(CALL(user32, DefWindowProcW, NULL, WM_NULL, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, (HWND)0x40000FF0, WM_NULL, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, NULL, WM_SETTEXT, 0, (LPARAM)u"x"), 0);
    T_EQ(CALL(user32, DefWindowProcW, (HWND)0x40000FF0, WM_GETTEXTLENGTH, 0, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h3, WM_NULL, 0, 0), 0);

    /* the core's stops, each on its own process (T_TRAPS) */
    ha = make_a(WS_POPUP);
    T_TRUE(ha != NULL);
    T_TRAPS(CALL(user32, DefWindowProcW, h, WM_GETMINMAXINFO, 0, 0), "rt_window_def_proc: not implemented");
    T_TRAPS(CALL(user32, DefWindowProcW, h, 0x0200, 0, 0), "rt_window_def_proc: not implemented");
    T_TRAPS(CALL(user32, DefWindowProcW, h, 0x10000, 0, 0), "rt_window_def_proc: not implemented");
    T_TRAPS(CALL(user32, DefWindowProcW, ha, WM_SETTEXT, 0, (LPARAM)u"x"), "rt_window_def_proc: not implemented");
    T_TRAPS(CALL(user32, DefWindowProcW, ha, WM_GETTEXT, 16, (LPARAM)buf), "rt_window_def_proc: not implemented");
}
