#include <string.h>

#include "rt/test.h"
#include "rt/window.h"

/* The RECT sits between two DWORDs filled with 0xA5, so a write past either end of it shows.
 * The clip is set and reset through ClipCursor, the other function of this family. */
TEST(user32, GetClipCursor)
{
    struct { DWORD before; RECT r; DWORD after; } s;
    RECT set = { 100, 100, 200, 200 };

    T_CASE("the whole screen at first");
    memset(&s, 0xA5, sizeof s);
    T_EQ(CALL(user32, GetClipCursor, &s.r), TRUE);
    T_EQ(s.r.left, 0);
    T_EQ(s.r.top, 0);
    T_EQ(s.r.right, 1920);
    T_EQ(s.r.bottom, 1080);

    T_CASE("the rectangle that ClipCursor set");
    T_EQ(CALL(user32, ClipCursor, &set), TRUE);
    memset(&s, 0xA5, sizeof s);
    T_EQ(CALL(user32, GetClipCursor, &s.r), TRUE);
    T_EQ(s.r.left, 100);
    T_EQ(s.r.top, 100);
    T_EQ(s.r.right, 200);
    T_EQ(s.r.bottom, 200);

    T_CASE("the whole screen after ClipCursor with NULL");
    T_EQ(CALL(user32, ClipCursor, NULL), TRUE);
    memset(&s, 0xA5, sizeof s);
    T_EQ(CALL(user32, GetClipCursor, &s.r), TRUE);
    T_EQ(s.r.left, 0);
    T_EQ(s.r.top, 0);
    T_EQ(s.r.right, 1920);
    T_EQ(s.r.bottom, 1080);

    T_CASE("writes the RECT and nothing else");
    memset(&s, 0xA5, sizeof s);
    T_EQ(CALL(user32, GetClipCursor, &s.r), TRUE);
    T_EQ(s.before, 0xA5A5A5A5u);
    T_EQ(s.after, 0xA5A5A5A5u);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetClipCursor, &s.r);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stop: the shim adds no guard for it. */
    T_TRAPS(CALL(user32, GetClipCursor, NULL), "rt_cursor_get_clip: not implemented");
}
