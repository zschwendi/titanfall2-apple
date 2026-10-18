#include "rt/test.h"
#include "rt/window.h"

/* The class procedure: WM_NCCREATE goes to the core's default processing, which stores the
 * title; every other message is answered with 0. */
GUEST_FN(find_proc)
{
    UINT msg = (UINT)ctx->r[2];
    ctx->r[0] = msg == WM_NCCREATE
        ? rt_window_def_proc(ctx, (HWND)(uintptr_t)ctx->r[1], msg, (WPARAM)ctx->r[8], (LPARAM)ctx->r[9], true)
        : 0;
    GUEST_RET(ctx);
}

static HWND make(Ctx *ctx, RtText title)
{
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"Valve001"), .title = title,
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(ctx, &a);
}

TEST(user32, FindWindowA)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(find_proc),
                       .lpszClassName = u"Valve001", .cbWndExtra = 16 };
    ATOM atom = rt_wndclass_register(&wc, true);
    HWND w1, w2;
    HWND none = (HWND)0;

    T_CASE("finds the first top-level window whose class matches (ASCII case ignored, an atom allowed)");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve001", NULL), none);     /* no window yet */
    T_EQ(T_LAST_ERROR(), 0x1234);
    w1 = make(T_CTX(), RT_TEXT_W(u"Title"));
    T_TRUE(w1 != none);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve001", NULL), w1);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"VALVE001", NULL), w1);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)(uintptr_t)atom, NULL), w1);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve002", NULL), none);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("finds by title");
    T_EQ(CALL(user32, FindWindowA, NULL, "Title"), w1);

    T_CASE("finds by class and title");
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve001", "Title"), w1);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve001", "Other"), none);

    T_CASE("NULL matches all");
    T_EQ(CALL(user32, FindWindowA, NULL, NULL), w1);

    T_CASE("an empty title matches only a window with no title");
    T_EQ(CALL(user32, FindWindowA, NULL, ""), none);                      /* w1 has a title */
    w2 = make(T_CTX(), RT_TEXT_W(u""));
    T_TRUE(w2 != none);
    T_EQ(CALL(user32, FindWindowA, NULL, ""), w2);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve001", ""), w2);
    T_EQ(CALL(user32, FindWindowA, NULL, "Title"), w1);

    T_CASE("NULL when nothing matches, with last error left alone");
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Nope", NULL), none);
    T_EQ(CALL(user32, FindWindowA, NULL, "Nope"), none);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, FindWindowA, (LPCVOID)"Valve001", "Titl"), none);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
