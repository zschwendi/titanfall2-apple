#include "rt/test.h"
#include "rt/win/user.h"

TEST(user32, AdjustWindowRectEx)
{
    RECT r;

    T_CASE("a style with no frame: the rectangle comes back unchanged and TRUE");
    r = (RECT){100, 100, 740, 580};
    T_EQ(CALL(user32, AdjustWindowRectEx, &r, WS_POPUP, FALSE, 0), TRUE);
    T_EQ(r.left, 100);
    T_EQ(r.top, 100);
    T_EQ(r.right, 740);
    T_EQ(r.bottom, 580);
    r = (RECT){100, 100, 740, 580};
    T_EQ(CALL(user32, AdjustWindowRectEx, &r, WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU, FALSE, 0), TRUE);
    T_EQ(r.left, 100);
    T_EQ(r.top, 100);
    T_EQ(r.right, 740);
    T_EQ(r.bottom, 580);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    r = (RECT){100, 100, 740, 580};
    T_EQ(CALL(user32, AdjustWindowRectEx, &r, WS_POPUP, FALSE, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Expected: rt/window.h, rt_window_frame (review N4's algorithm; caption height 22 is this
     * desktop's).  The second call is the one the real game makes before it creates its window
     * (docs/trace/window-calls.md), for a 1920 x 1080 client area at the origin. */
    T_CASE("a caption and fixed frame grow the rectangle");
    r = (RECT){100, 100, 740, 580};
    T_EQ(CALL(user32, AdjustWindowRectEx, &r, WS_CAPTION, FALSE, 0), TRUE);
    T_TRUE(r.left == 97 && r.top == 74 && r.right == 743 && r.bottom == 583);
    r = (RECT){0, 0, 1920, 1080};
    T_EQ(CALL(user32, AdjustWindowRectEx, &r, 0x84ca0000u, FALSE, 0x00040300u), TRUE);
    T_TRUE(r.left == -5 && r.top == -28 && r.right == 1925 && r.bottom == 1085);

    /* RT-TRAPS: the frames this desktop has no number for, and a NULL rectangle (not documented). */
    T_TRAPS(CALL(user32, AdjustWindowRectEx, &r, WS_POPUP | WS_THICKFRAME, FALSE, 0), "a frame this desktop has no size for: style 0x80040000");
    T_TRAPS(CALL(user32, AdjustWindowRectEx, &r, WS_CAPTION, FALSE, WS_EX_TOOLWINDOW), "a frame this desktop has no size for: style 0xc00000, extended style 0x80");
    T_TRAPS(CALL(user32, AdjustWindowRectEx, &r, WS_POPUP, TRUE, 0), "a frame this desktop has no size for: style 0x80000000, extended style 0x0, menu 1");
    T_TRAPS(CALL(user32, AdjustWindowRectEx, NULL, WS_POPUP, FALSE, 0), "a NULL rectangle");
}
