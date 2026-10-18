#include <string.h>

#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/error.h"

/* The clip is read back through GetClipCursor, and the cursor through rt_cursor_get_pos,
 * the core's own read of the position (the guest has no other way to read it here). */
TEST(user32, ClipCursor)
{
    RECT clip = { 100, 100, 200, 200 };
    RECT inv = { 50, 50, 49, 49 };
    RECT empty_lr = { 50, 50, 50, 90 };
    RECT empty_tb = { 50, 50, 90, 50 };
    RECT off = { 2000, 100, 2100, 200 };
    RECT got;
    POINT p;

    T_CASE("a rectangle becomes the clip and moves a cursor outside it inside");
    T_EQ(rt_cursor_get_pos(&p), TRUE);
    T_EQ(p.x, 960);
    T_EQ(p.y, 540);
    T_EQ(CALL(user32, ClipCursor, &clip), TRUE);
    T_EQ(CALL(user32, GetClipCursor, &got), TRUE);
    T_EQ(got.left, 100);
    T_EQ(got.top, 100);
    T_EQ(got.right, 200);
    T_EQ(got.bottom, 200);
    T_EQ(rt_cursor_get_pos(&p), TRUE);
    T_EQ(p.x, 199);
    T_EQ(p.y, 199);

    T_CASE("an inverted rectangle is refused with ERROR_ACCESS_DENIED and changes nothing");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, ClipCursor, &inv), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(CALL(user32, GetClipCursor, &got), TRUE);
    T_EQ(got.left, 100);
    T_EQ(got.top, 100);
    T_EQ(got.right, 200);
    T_EQ(got.bottom, 200);
    T_EQ(rt_cursor_get_pos(&p), TRUE);
    T_EQ(p.x, 199);
    T_EQ(p.y, 199);

    T_CASE("NULL restores the whole screen");
    T_EQ(CALL(user32, ClipCursor, NULL), TRUE);
    memset(&got, 0xA5, sizeof got);
    T_EQ(CALL(user32, GetClipCursor, &got), TRUE);
    T_EQ(got.left, 0);
    T_EQ(got.top, 0);
    T_EQ(got.right, 1920);
    T_EQ(got.bottom, 1080);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, ClipCursor, &clip), TRUE);
    T_EQ(CALL(user32, ClipCursor, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops: the shim adds no guard for them. */
    T_TRAPS(CALL(user32, ClipCursor, &empty_lr), "rt_cursor_clip: not implemented");
    T_TRAPS(CALL(user32, ClipCursor, &empty_tb), "rt_cursor_clip: not implemented");
    T_TRAPS(CALL(user32, ClipCursor, &off), "rt_cursor_clip: not implemented");
}
