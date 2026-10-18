#include "rt/test.h"
#include "rt/win/user.h"

/* A RECT followed by the 8 bytes that the function must not touch: they hold 0xA5 before
 * each call, and every one of them is checked after it. */
struct guarded {
    RECT rc;
    unsigned char after[8];
};

TEST(user32, SystemParametersInfoA)
{
    struct guarded g;
    unsigned int i;

    T_CASE("SPI_GETWORKAREA copies the work area into the RECT and returns TRUE");
    for (i = 0; i < sizeof g; i++) ((unsigned char *)&g)[i] = 0xA5;
    T_EQ(CALL(user32, SystemParametersInfoA, SPI_GETWORKAREA, 0, &g.rc, 0), TRUE);
    T_EQ(g.rc.left, 0);                     /* member by member, in the order of the structure */
    T_EQ(g.rc.top, 0);
    T_EQ(g.rc.right, 1920);
    T_EQ(g.rc.bottom, 1080);
    for (i = 0; i < 8; i++) T_EQ(g.after[i], 0xA5);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SystemParametersInfoA, SPI_GETWORKAREA, 0, &g.rc, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);           /* success leaves last error alone */

    /* RT-TRAPS: an action other than SPI_GETWORKAREA, and a NULL pvParam, are never guessed.
     * The number is written out for 0x68 because rt/win/ does not define SPI_GETWHEELSCROLLLINES. */
    T_TRAPS(CALL(user32, SystemParametersInfoA, 0x68, 0, &g.rc, 0),
            "an action other than SPI_GETWORKAREA with a RECT");
    T_TRAPS(CALL(user32, SystemParametersInfoA, SPI_GETWORKAREA, 0, NULL, 0),
            "an action other than SPI_GETWORKAREA with a RECT");
}
