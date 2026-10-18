#include <string.h>

#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

/* The window procedure keeps the text it receives in WM_SETTEXT (wParam unused, lParam the
 * string) and answers WM_GETTEXT from it, so the check reads the copy the guest procedure holds. */
static char text[64];

GUEST_FN(proc)
{
    const UINT msg = (UINT)ctx->r[2];
    if (msg == WM_SETTEXT) {
        strncpy(text, (const char *)(uintptr_t)ctx->r[9], sizeof text - 1);
        text[sizeof text - 1] = 0;
        ctx->r[0] = 1;
    } else {
        ctx->r[0] = (msg == WM_NCCREATE);
    }
    GUEST_RET(ctx);
}

/* A window of an A class (narrow strings, as CreateWindowExA makes it) or of a W class. */
static HWND mk(bool unicode)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc),
                       .lpszClassName = unicode ? u"SXW" : (LPCWSTR)(uintptr_t)"SXA" };
    T_TRUE(rt_wndclass_register(&wc, unicode) != 0);
    RtWindowCreate a = { .style = WS_POPUP,
                         .cls = unicode ? RT_TEXT_W(u"SXW") : RT_TEXT_A("SXA"),
                         .title = unicode ? RT_TEXT_W(u"") : RT_TEXT_A(""),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = unicode };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, SetWindowTextA)
{
    HWND h = mk(false);
    T_REQUIRE(h != NULL);
    HWND w = mk(true);
    T_REQUIRE(w != NULL);

    T_CASE("sets the text the window's procedure receives and returns TRUE");
    T_EQ(CALL(user32, SetWindowTextA, h, "Hello"), TRUE);
    T_TRUE(strcmp(text, "Hello") == 0);

    T_CASE("the empty string clears the text");
    T_EQ(CALL(user32, SetWindowTextA, h, ""), TRUE);
    T_TRUE(strcmp(text, "") == 0);

    T_CASE("a success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetWindowTextA, h, "again"), TRUE);
    T_TRUE(strcmp(text, "again") == 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a window that does not exist returns FALSE with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, SetWindowTextA, (HWND)(uintptr_t)0x12345, "x"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(strcmp(text, "again") == 0);

    T_TRAPS(CALL(user32, SetWindowTextA, w, "x"), "rt_window_send: not implemented");
}
