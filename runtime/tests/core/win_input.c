/* Cursor and keyboard state (rt/window.h, core/win_input.c): the position, the clip, the
 * display counter, the current cursor and the key state.  Expected values: the comments of
 * rt_cursor_get_pos, rt_cursor_set_pos, rt_cursor_clip, rt_cursor_get_clip, rt_cursor_show,
 * rt_cursor_set and rt_key_state.  Every TEST is one process; the cases of a TEST run in order. */
#include "rt/test.h"
#include "../../core/win_priv.h"
#include "rt/win/user.h"

TEST(core, win_input)
{
    T_PLAN(1, "the position starts in the middle of the screen") {
        POINT p = { -1, -1 };
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(p.x, 960);
        T_EQ(p.y, 540);
    }

    T_PLAN(2, "get_pos with no place for the answer fails") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_cursor_get_pos(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
    }

    T_PLAN(3, "set_pos stores the position") {
        POINT p = { -1, -1 };
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_cursor_set_pos(100, 200));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(p.x, 100);
        T_EQ(p.y, 200);
    }

    T_PLAN(4, "set_pos clamps the position to the screen") {
        POINT p = { -1, -1 };
        T_TRUE(rt_cursor_set_pos(-5, 5000));
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(p.x, 0);
        T_EQ(p.y, 1079);
        T_TRUE(rt_cursor_set_pos(1920, -1));
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(p.x, 1919);
        T_EQ(p.y, 0);
    }
}

TEST(core, win_input_clip)
{
    T_PLAN(5, "the clip starts as the whole screen") {
        RECT r = { 9, 9, 9, 9 };
        T_TRUE(rt_cursor_get_clip(&r));
        T_EQ(r.left, 0);
        T_EQ(r.top, 0);
        T_EQ(r.right, 1920);
        T_EQ(r.bottom, 1080);
    }

    T_PLAN(6, "a clip moves a cursor outside it to the nearest point inside") {
        RECT c = { 100, 100, 200, 200 };
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_cursor_clip(&c));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        RECT g = { 9, 9, 9, 9 };
        T_TRUE(rt_cursor_get_clip(&g));
        T_EQ(g.left, 100);
        T_EQ(g.top, 100);
        T_EQ(g.right, 200);
        T_EQ(g.bottom, 200);
        POINT p = { -1, -1 };
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(p.x, 199);
        T_EQ(p.y, 199);
    }

    T_PLAN(7, "inside the clip the position is set; inverted clips are refused") {
        T_TRUE(rt_cursor_set_pos(0, 150));
        POINT p = { -1, -1 };
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(p.x, 100);
        T_EQ(p.y, 150);
        RECT v = { 50, 50, 49, 49 };
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_cursor_clip(&v), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        RECT v2 = { 50, 50, 60, 40 };
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_cursor_clip(&v2), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        RECT g = { 9, 9, 9, 9 };
        T_TRUE(rt_cursor_get_clip(&g));
        T_EQ(g.left, 100);
        T_EQ(g.top, 100);
        T_EQ(g.right, 200);
        T_EQ(g.bottom, 200);
        T_TRUE(rt_cursor_get_pos(&p));
        T_EQ(p.x, 100);
        T_EQ(p.y, 150);
    }

    T_PLAN(8, "a clip is intersected with the screen") {
        RECT c = { -50, -50, 100, 3000 };
        T_TRUE(rt_cursor_clip(&c));
        RECT g = { 9, 9, 9, 9 };
        T_TRUE(rt_cursor_get_clip(&g));
        T_EQ(g.left, 0);
        T_EQ(g.top, 0);
        T_EQ(g.right, 100);
        T_EQ(g.bottom, 1080);
    }

    T_PLAN(9, "a NULL clip restores the whole screen and does not move the cursor") {
        POINT before = { -1, -1 };
        T_TRUE(rt_cursor_get_pos(&before));
        T_TRUE(rt_cursor_clip(NULL));
        RECT g = { 9, 9, 9, 9 };
        T_TRUE(rt_cursor_get_clip(&g));
        T_EQ(g.left, 0);
        T_EQ(g.top, 0);
        T_EQ(g.right, 1920);
        T_EQ(g.bottom, 1080);
        POINT after = { -1, -1 };
        T_TRUE(rt_cursor_get_pos(&after));
        T_EQ(after.x, before.x);
        T_EQ(after.y, before.y);
    }

    T_PLAN(10, "a clip with no point on the screen, an empty clip and a NULL out stop") {
        RECT c = { 2000, 0, 2100, 100 };
        T_TRAPS(rt_cursor_clip(&c), "rt_cursor_clip: not implemented");
        RECT e = { 50, 50, 50, 90 };
        T_TRAPS(rt_cursor_clip(&e), "rt_cursor_clip: not implemented");
        RECT e2 = { 50, 50, 90, 50 };
        T_TRAPS(rt_cursor_clip(&e2), "rt_cursor_clip: not implemented");
        T_TRAPS(rt_cursor_get_clip(NULL), "rt_cursor_get_clip: not implemented");
    }
}

TEST(core, win_input_show)
{
    T_PLAN(11, "the display counter goes down and up by one") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_cursor_show(FALSE), -1);
        T_EQ(rt_cursor_show(TRUE), 0);
        T_EQ(rt_cursor_show(TRUE), 1);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }
}

TEST(core, win_input_set)
{
    T_PLAN(12, "SetCursor returns the previous cursor") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_cursor_set(RT_HCURSOR_SYSTEM(IDC_ARROW)) == NULL);
        T_TRUE(rt_cursor_set(NULL) == RT_HCURSOR_SYSTEM(IDC_ARROW));
        T_TRUE(rt_cursor_set(RT_HCURSOR_SYSTEM(IDC_HELP)) == NULL);
        T_TRUE(rt_cursor_set(RT_HCURSOR_SYSTEM(IDC_SIZEALL)) == RT_HCURSOR_SYSTEM(IDC_HELP));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(13, "a handle that is no system cursor stops") {
        T_TRAPS(rt_cursor_set((HCURSOR)0x1234), "rt_cursor_set: not implemented");
        T_TRAPS(rt_cursor_set(RT_HCURSOR_SYSTEM(100)), "rt_cursor_set: not implemented");
        T_TRAPS(rt_cursor_set(RT_HCURSOR_SYSTEM(32647)), "rt_cursor_set: not implemented");
        T_TRAPS(rt_cursor_set((HCURSOR)RT_HICON_SYSTEM(IDC_ARROW)), "rt_cursor_set: not implemented");
    }

    T_PLAN(14, "every key from 0 to 255 is up and not toggled") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_key_state(0), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_key_state(65), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_key_state(255), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(15, "a virtual key outside 0 to 255 stops") {
        T_TRAPS(rt_key_state(256), "rt_key_state: not implemented");
        T_TRAPS(rt_key_state(-1), "rt_key_state: not implemented");
    }
}
