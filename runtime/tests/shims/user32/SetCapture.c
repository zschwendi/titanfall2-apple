#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }  /* rdx: the message */

/* A popup window of this test's class, made through the core (no other shim).  Its procedure
 * is never called: no message reaches it. */
static HWND make_window(Ctx *ctx)
{
    static bool registered;
    if (!registered) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
        rt_wndclass_register(&wc, true);
        registered = true;
    }
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(ctx, &a);
}

TEST(user32, SetCapture)
{
    HWND a = make_window(T_CTX());
    HWND b = make_window(T_CTX());
    HWND c = make_window(T_CTX());
    T_TRUE(a && b && c);

    T_CASE("captures the mouse for a window and returns NULL when nothing was captured");
    T_EQ(CALL(user32, SetCapture, a), NULL);

    T_CASE("capturing again with the window that holds the capture returns it");
    T_EQ(CALL(user32, SetCapture, a), a);
    T_EQ(CALL(user32, SetCapture, a), a);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, SetCapture, a);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (rt_window_set_capture), proved after the checks of the last case.
     * A window of another thread is not tested: that needs a second thread. */
    T_TRAPS(CALL(user32, SetCapture, b), "rt_window_set_capture: not implemented");
    T_TRAPS(CALL(user32, SetCapture, NULL), "rt_window_set_capture: not implemented");
    T_TRAPS(CALL(user32, SetCapture, (HWND)0x40000FF0), "rt_window_set_capture: not implemented");
    T_EQ(rt_window_destroy(T_CTX(), c), TRUE);
    T_TRAPS(CALL(user32, SetCapture, c), "rt_window_set_capture: not implemented");
}
