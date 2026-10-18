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

TEST(user32, ReleaseCapture)
{
    HWND a = make_window(T_CTX());
    HWND b = make_window(T_CTX());
    T_TRUE(a && b);

    T_CASE("releases the capture and returns TRUE");
    T_EQ(CALL(user32, SetCapture, a), NULL);
    T_EQ(CALL(user32, ReleaseCapture), TRUE);
    T_EQ(CALL(user32, SetCapture, b), NULL);    /* cleared: b captures now, with no stop */

    T_CASE("returns TRUE when nothing was captured");
    T_EQ(CALL(user32, ReleaseCapture), TRUE);   /* releases b's capture */
    T_EQ(CALL(user32, ReleaseCapture), TRUE);   /* nothing is captured now */

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, ReleaseCapture);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
