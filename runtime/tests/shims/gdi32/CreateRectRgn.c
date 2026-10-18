#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core: a region made by CreateRectRgn is a live region object, so the
 * handle is not NULL and rt_gdi_delete takes it once.  The core keeps no query of a region's
 * rectangle that the guest can reach, so the corners are not checked here. */
TEST(gdi32, CreateRectRgn)
{
    HRGN r, e;

    T_CASE("returns a handle to a region for a rectangle");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(gdi32, CreateRectRgn, 10, 20, 110, 220);
    T_TRUE(r != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);                  /* success leaves last error alone */
    T_EQ(rt_gdi_delete((HGDIOBJ)r), TRUE);

    T_CASE("an empty rectangle gives a region too (the empty region)");
    e = CALL(gdi32, CreateRectRgn, 5, 5, 5, 5);
    T_TRUE(e != NULL);
    T_EQ(rt_gdi_delete((HGDIOBJ)e), TRUE);
}
