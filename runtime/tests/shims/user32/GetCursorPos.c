#include "rt/test.h"

/* The POINT sits between two sentinel words, so a write outside the eight bytes of the
 * structure shows.  Each test runs in its own process: the first call starts the core's
 * position at {960, 540}. */
struct guarded {
    uint32_t before;
    POINT p;
    uint32_t after;
};

#define SENTINEL 0xDEADBEEFu

TEST(user32, GetCursorPos)
{
    struct guarded g;

    T_CASE("starts at the middle of the screen");
    g.before = SENTINEL;
    g.after = SENTINEL;
    g.p.x = -1;
    g.p.y = -1;
    T_EQ(CALL(user32, GetCursorPos, &g.p), TRUE);
    T_EQ(g.p.x, 960);
    T_EQ(g.p.y, 540);
    T_EQ(g.before, SENTINEL);           /* nothing written before the POINT */
    T_EQ(g.after, SENTINEL);            /* nothing written after the POINT */

    T_CASE("reports the position last set by SetCursorPos");
    T_EQ(CALL(user32, SetCursorPos, 100, 200), TRUE);
    g.p.x = -1;
    g.p.y = -1;
    T_EQ(CALL(user32, GetCursorPos, &g.p), TRUE);
    T_EQ(g.p.x, 100);
    T_EQ(g.p.y, 200);
    T_EQ(g.before, SENTINEL);
    T_EQ(g.after, SENTINEL);

    T_CASE("a NULL pointer fails with ERROR_NOACCESS");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetCursorPos, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetCursorPos, &g.p), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
