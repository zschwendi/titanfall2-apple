#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* Expected values: the core's comment for a new memory context (TA_LEFT | TA_TOP, 0), the
 * Microsoft page for SetTextAlign's return value, and the core's rule for a handle that is
 * not a context (from memory, rt/gdi.h). */
TEST(gdi32, SetTextAlign)
{
    HDC dc = rt_dc_create_compatible(NULL);
    T_REQUIRE(dc != NULL);

    T_CASE("returns the previous alignment and sets the new one");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetTextAlign, dc, TA_RIGHT), TA_LEFT | TA_TOP);
    T_EQ(CALL(gdi32, SetTextAlign, dc, 6), TA_RIGHT);
    T_EQ(CALL(gdi32, SetTextAlign, dc, TA_LEFT | TA_TOP), 6);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("a handle that is no context returns GDI_ERROR with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetTextAlign, (HDC)0xdeadbeef, TA_RIGHT), 0xFFFFFFFFu);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_EQ(rt_dc_delete(dc), TRUE);
}
