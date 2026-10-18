#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core: SetRectRgn is TRUE for a region handle and FALSE for a handle that
 * is not a region (a brush, NULL).  The core sets no last error here (rt/gdi.h), so a
 * failure leaves it alone too. */
TEST(gdi32, SetRectRgn)
{
    HRGN r;
    HGDIOBJ b;

    T_CASE("sets the rectangle of a region handle");
    r = rt_gdi_create_rect_rgn(0, 0, 10, 10);
    T_REQUIRE(r != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, SetRectRgn, r, 1, 2, 30, 40), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);                  /* success leaves last error alone */
    T_EQ(CALL(gdi32, SetRectRgn, r, 50, 60, 50, 60), TRUE);   /* an empty rectangle is a valid set */
    T_EQ(rt_gdi_delete((HGDIOBJ)r), TRUE);

    T_CASE("a brush and NULL fail");
    b = rt_gdi_create_brush(0x00030201u);
    T_REQUIRE(b != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, SetRectRgn, (HRGN)b, 1, 2, 3, 4), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);                  /* the core leaves last error alone on failure */
    T_EQ(CALL(gdi32, SetRectRgn, NULL, 1, 2, 3, 4), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_gdi_delete(b), TRUE);
}
