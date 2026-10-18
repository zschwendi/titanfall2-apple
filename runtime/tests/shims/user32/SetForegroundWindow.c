#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: the comment of rt_window_set_foreground in rt/window.h (win.c,
 * test_SetForegroundWindow; V36).  A valid record STOPS in the core ("activation needs a
 * visible window": Gate 4), so the one case that reaches it is proved with T_TRAPS. */
GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

TEST(user32, SetForegroundWindow)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(T_CTX(), &a);
    T_REQUIRE(h != NULL);
    HWND gone = rt_window_create(T_CTX(), &a);
    T_REQUIRE(gone != NULL);
    T_REQUIRE(rt_window_destroy(T_CTX(), gone));

    T_CASE("a NULL handle");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetForegroundWindow, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("a handle that is no window");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetForegroundWindow, (HWND)0x40000FF0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetForegroundWindow, gone), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_TRAPS(CALL(user32, SetForegroundWindow, h), "rt_window_set_foreground: not implemented");
}
