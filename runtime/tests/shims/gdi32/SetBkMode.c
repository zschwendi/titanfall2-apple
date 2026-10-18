#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* Expected values: Wine gdi32/tests/dc.c (test_dc_values) for the handle, and the
 * Microsoft page for SetBkMode's return value.  A new memory context starts OPAQUE (rt/gdi.h). */
TEST(gdi32, SetBkMode)
{
    HDC dc = rt_dc_create_compatible(NULL);
    T_REQUIRE(dc != NULL);

    T_CASE("returns the previous mode and sets the new one");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetBkMode, dc, TRANSPARENT), OPAQUE);
    T_EQ(CALL(gdi32, SetBkMode, dc, OPAQUE), TRANSPARENT);
    T_EQ(CALL(gdi32, SetBkMode, dc, OPAQUE), OPAQUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("a mode other than TRANSPARENT and OPAQUE returns 0 and changes nothing");
    T_EQ(CALL(gdi32, SetBkMode, dc, TRANSPARENT), OPAQUE);
    T_EQ(CALL(gdi32, SetBkMode, dc, 7), 0);
    T_EQ(CALL(gdi32, SetBkMode, dc, OPAQUE), TRANSPARENT);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("a handle that is no context returns 0 with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetBkMode, (HDC)0xdeadbeef, OPAQUE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetBkMode, dc, OPAQUE), OPAQUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_EQ(rt_dc_delete(dc), TRUE);
}
