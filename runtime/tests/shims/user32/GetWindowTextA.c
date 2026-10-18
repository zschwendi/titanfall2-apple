#include <string.h>

#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

/* The window procedure keeps the text itself: WM_SETTEXT copies the string in, WM_GETTEXT copies
 * at most wParam - 1 characters out and a terminator after them, and returns the count. */
static char text[64];

GUEST_FN(proc)
{
    const UINT msg = (UINT)ctx->r[2];
    if (msg == WM_SETTEXT) {
        strncpy(text, (const char *)(uintptr_t)ctx->r[9], sizeof text - 1);
        text[sizeof text - 1] = 0;
        ctx->r[0] = 1;
    } else if (msg == WM_GETTEXT) {
        size_t cap = (size_t)ctx->r[8];
        char *out = (char *)(uintptr_t)ctx->r[9];
        size_t n = strlen(text);
        if (n > cap - 1) n = cap - 1;
        memcpy(out, text, n);
        out[n] = 0;
        ctx->r[0] = n;
    } else {
        ctx->r[0] = (msg == WM_NCCREATE);
    }
    GUEST_RET(ctx);
}

/* A window of an A class (narrow strings, as CreateWindowExA makes it) or of a W class. */
static HWND mk(bool unicode)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc),
                       .lpszClassName = unicode ? u"TXW" : (LPCWSTR)(uintptr_t)"TXA" };
    T_TRUE(rt_wndclass_register(&wc, unicode) != 0);
    RtWindowCreate a = { .style = WS_POPUP,
                         .cls = unicode ? RT_TEXT_W(u"TXW") : RT_TEXT_A("TXA"),
                         .title = unicode ? RT_TEXT_W(u"") : RT_TEXT_A(""),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = unicode };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, GetWindowTextA)
{
    char buf[8];
    HWND h = mk(false);
    T_REQUIRE(h != NULL);
    HWND w = mk(true);
    T_REQUIRE(w != NULL);

    T_CASE("the text set on the window");
    T_REQUIRE(rt_window_set_text(T_CTX(), h, RT_TEXT_A("Hello")));
    memset(buf, 0x55, sizeof buf);
    T_EQ(CALL(user32, GetWindowTextA, h, buf, 8), 5);
    T_TRUE(strcmp(buf, "Hello") == 0);
    T_EQ((unsigned char)buf[6], 0x55);
    T_EQ((unsigned char)buf[7], 0x55);

    T_CASE("a buffer of 4 gets 3 characters and a terminator");
    memset(buf, 0x55, sizeof buf);
    T_EQ(CALL(user32, GetWindowTextA, h, buf, 4), 3);
    T_TRUE(memcmp(buf, "Hel\0", 4) == 0);
    T_EQ((unsigned char)buf[4], 0x55);

    T_CASE("cap 0 returns 0 and leaves the buffer alone");
    memset(buf, 0x55, sizeof buf);
    T_EQ(CALL(user32, GetWindowTextA, h, buf, 0), 0);
    for (unsigned i = 0; i < sizeof buf; i++) T_EQ((unsigned char)buf[i], 0x55);

    T_CASE("a window that does not exist returns 0 with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowTextA, (HWND)(uintptr_t)0x12345, buf, 8), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("a success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetWindowTextA, h, buf, 8), 5);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, GetWindowTextA, w, buf, 8), "rt_window_send: not implemented");
}
