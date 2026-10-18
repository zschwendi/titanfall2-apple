#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core: rt_dc_window names a live memory context with -1 and a context that
 * is gone with 0 (rt/gdi.h). */
TEST(gdi32, DeleteDC)
{
    HDC s = rt_dc_get(NULL);
    HDC m;
    HWND w;

    T_REQUIRE(s != NULL);

    T_CASE("deletes a memory context once and fails the second time");
    m = rt_dc_create_compatible(NULL);
    T_REQUIRE(m != NULL);
    T_EQ(rt_dc_window(m, &w, NULL), -1);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, DeleteDC, m), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* success leaves last error alone */
    T_EQ(rt_dc_window(m, &w, NULL), 0);           /* the record is gone */
    T_EQ(CALL(gdi32, DeleteDC, m), FALSE);
    T_EQ(rt_dc_window(m, &w, NULL), 0);

    T_CASE("a handle that is no context fails");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, DeleteDC, (HDC)0x40000FF0), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* no last error set: from memory, review S26 */
    T_EQ(CALL(gdi32, DeleteDC, NULL), FALSE);

    T_TRAPS(CALL(gdi32, DeleteDC, s), "rt_dc_delete: not implemented");
}
