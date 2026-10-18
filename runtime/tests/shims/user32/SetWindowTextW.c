#include <string.h>

#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

/* The window procedure keeps the title it is sent: WM_SETTEXT copies the WCHAR string, and
 * WM_GETTEXT copies it back, as a window of this kind does.  The text the test reads is the
 * text this procedure holds, so the check sees what the guest would see. */
static WCHAR stored[256];
static UINT last_msg;

GUEST_FN(keep)
{
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = (WPARAM)ctx->r[8];
    WCHAR *lp = (WCHAR *)(uintptr_t)ctx->r[9];
    size_t n = 0;

    last_msg = msg;
    ctx->r[0] = 0;
    if (msg == WM_NCCREATE) {
        ctx->r[0] = TRUE;
    } else if (msg == WM_SETTEXT) {
        while (n < 255 && lp[n]) n++;
        memcpy(stored, lp, n * sizeof(WCHAR));
        stored[n] = 0;
        ctx->r[0] = TRUE;
    } else if (msg == WM_GETTEXT) {
        while (stored[n]) n++;
        if (wp == 0) n = 0;
        else if (n > wp - 1) n = wp - 1;
        memcpy(lp, stored, n * sizeof(WCHAR));
        lp[n] = 0;
        ctx->r[0] = n;
    }
    GUEST_RET(ctx);
}

static HWND make_w(void)
{
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"TW"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, SetWindowTextW)
{
    WNDCLASSEXW cw = { .cbSize = sizeof cw, .lpfnWndProc = T_GUEST_ADDR(keep), .lpszClassName = u"TW", .cbWndExtra = 16 };
    WNDCLASSEXA ca = { .cbSize = sizeof ca, .lpfnWndProc = T_GUEST_ADDR(keep), .lpszClassName = "TA", .cbWndExtra = 16 };
    static const WCHAR src[] = u"abc";
    static const WCHAR uni[] = u"é中";
    WCHAR buf[256];
    HWND h, h2, ha;

    rt_wndclass_register(&cw, true);
    rt_wndclass_register((const WNDCLASSEXW *)&ca, false);

    T_CASE("sets the window text of a window of the calling thread and returns TRUE");
    h = make_w();
    T_TRUE(h != NULL);
    stored[0] = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetWindowTextW, h, src), TRUE);
    T_EQ(last_msg, WM_SETTEXT);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* success leaves last error alone */
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 256, true), 3);
    T_TRUE(buf[0] == u'a' && buf[1] == u'b' && buf[2] == u'c' && buf[3] == 0);
    T_TRUE(src[0] == u'a' && src[1] == u'b' && src[2] == u'c' && src[3] == 0);   /* the source is not written */

    T_CASE("the text is UTF-16 code units, not only ASCII");
    T_EQ(CALL(user32, SetWindowTextW, h, uni), TRUE);
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 256, true), 2);
    T_TRUE(buf[0] == 0x00e9 && buf[1] == 0x4e2d && buf[2] == 0);

    T_CASE("an empty string sets an empty text");
    T_EQ(CALL(user32, SetWindowTextW, h, u""), TRUE);
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 256, true), 0);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x4321);
    T_EQ(CALL(user32, SetWindowTextW, h, src), TRUE);
    T_EQ(T_LAST_ERROR(), 0x4321);
    rt_window_destroy(T_CTX(), h);

    T_CASE("an invalid window returns FALSE with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowTextW, NULL, src), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowTextW, (HWND)0x40000FF0, src), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    h2 = make_w();
    T_TRUE(h2 != NULL);
    rt_window_destroy(T_CTX(), h2);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowTextW, h2, src), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    /* core: WM_SETTEXT through the W function to an A window */
    {
        RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_A("TA"), .title = RT_TEXT_A("t"),
                             .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = false };
        ha = rt_window_create(T_CTX(), &a);
        T_TRUE(ha != NULL);
    }
    T_TRAPS(CALL(user32, SetWindowTextW, ha, src), "rt_window_send: not implemented");
}
