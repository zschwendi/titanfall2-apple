#include "rt/test.h"
#include "rt/window.h"

/* Expected values: the comments of rt_window_set_active in rt/window.h (Wine
 * user32/tests/win.c, test_SetActiveWindow_0_proc; review D4; msg.c, test_SetFocus; V36). */
GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

static HWND make(void)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, SetActiveWindow)
{
    HWND h;

    T_CASE("NULL: returns NULL, last error untouched");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetActiveWindow, NULL), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is not a window: NULL with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetActiveWindow, (HWND)0x40000FF0), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    h = make();
    T_REQUIRE(h != NULL);
    T_EQ(rt_window_destroy(T_CTX(), h), TRUE);
    T_EQ(CALL(user32, SetActiveWindow, h), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    /* core: a valid window (activation needs a visible window) */
    h = make();
    T_REQUIRE(h != NULL);
    T_TRAPS(CALL(user32, SetActiveWindow, h), "rt_window_set_active: not implemented");
}
