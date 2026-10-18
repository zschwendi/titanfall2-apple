#include "rt/test.h"

/* A RECT followed by the 8 bytes that the function must not touch: they hold 0xA5 before
 * each call, and every one of them is checked after it. */
struct guarded {
    RECT rc;
    unsigned char after[8];
};

TEST(user32, SetRect)
{
    struct guarded g;
    BOOL r;
    unsigned int i;

    T_CASE("stores the four values in the RECT and returns TRUE");
    for (i = 0; i < sizeof g; i++) ((unsigned char *)&g)[i] = 0xA5;
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(user32, SetRect, &g.rc, 1, -2, 3, 4);
    T_EQ(r, TRUE);
    T_EQ(g.rc.left, 1);                     /* member by member, in the order of the structure */
    T_EQ(g.rc.top, -2);
    T_EQ(g.rc.right, 3);
    T_EQ(g.rc.bottom, 4);
    for (i = 0; i < 8; i++) T_EQ(g.after[i], 0xA5);
    T_EQ(T_LAST_ERROR(), 0x1234);           /* success leaves last error alone */

    T_CASE("a NULL RECT returns FALSE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(user32, SetRect, NULL, 1, -2, 3, 4);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);           /* no last error is set on this failure */
}
