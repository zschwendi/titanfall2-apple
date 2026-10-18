#include "rt/test.h"
#include "rt/window.h"

/* The callback records every window it is given, and the lParam of the last call. */
static unsigned g_calls;
static uint64_t g_seen[4];
static uint64_t g_lparam;
static bool g_stop_zero;

GUEST_FN(enum_cb)
{
    if (g_calls < 4) g_seen[g_calls] = ctx->r[1];
    g_lparam = ctx->r[2];
    g_calls++;
    ctx->r[0] = g_stop_zero ? 0 : 1;
    GUEST_RET(ctx);
}

GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

static HWND make_window(void)
{
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, EnumWindows)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    HWND a, b;
    BOOL r;

    rt_wndclass_register(&wc, true);

    T_CASE("no windows: callback never called and TRUE");
    g_calls = 0;
    r = CALL(user32, EnumWindows, T_GUEST_ADDR(enum_cb), 0x1234);
    T_EQ(r, TRUE);
    T_EQ(g_calls, 0);

    a = make_window();
    b = make_window();
    T_REQUIRE(a && b);

    T_CASE("callback sees each window, newest first, with lParam");
    g_calls = 0;
    g_lparam = 0;
    r = CALL(user32, EnumWindows, T_GUEST_ADDR(enum_cb), 0xBEEF);
    T_EQ(r, TRUE);
    T_EQ(g_calls, 2);
    T_EQ((uintptr_t)g_seen[0], (uintptr_t)b);
    T_EQ((uintptr_t)g_seen[1], (uintptr_t)a);
    T_EQ(g_lparam, 0xBEEF);

    T_CASE("callback returning 0 stops the enumeration and returns FALSE");
    g_calls = 0;
    g_stop_zero = true;
    r = CALL(user32, EnumWindows, T_GUEST_ADDR(enum_cb), 0);
    g_stop_zero = false;
    T_EQ(r, FALSE);
    T_EQ(g_calls, 1);
    T_EQ((uintptr_t)g_seen[0], (uintptr_t)b);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(user32, EnumWindows, T_GUEST_ADDR(enum_cb), 0);
    T_EQ(r, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: after the checks of the last case. */
    T_TRAPS(CALL(user32, EnumWindows, 0, 0), "lpEnumFunc NULL");
    for (unsigned i = 2; i < 257; i++) T_REQUIRE(make_window());
    T_TRAPS(CALL(user32, EnumWindows, T_GUEST_ADDR(enum_cb), 0), "more than 256");
}
