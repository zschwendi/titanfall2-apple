#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* Expected values: the Microsoft page for SetStretchBltMode (the four modes, the previous mode
 * as the return value, 0 on failure) and rt/gdi.h for the codes.  A new memory context starts
 * BLACKONWHITE (rt/gdi.h). */
TEST(gdi32, SetStretchBltMode)
{
    HDC dc = rt_dc_create_compatible(NULL);
    T_REQUIRE(dc != NULL);

    T_CASE("returns the previous mode and sets the new one");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, COLORONCOLOR), BLACKONWHITE);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, WHITEONBLACK), COLORONCOLOR);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, HALFTONE), WHITEONBLACK);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, BLACKONWHITE), HALFTONE);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, COLORONCOLOR), BLACKONWHITE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("a mode outside BLACKONWHITE to HALFTONE returns 0 with ERROR_INVALID_PARAMETER and changes nothing");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, 9), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, 5), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, COLORONCOLOR), COLORONCOLOR);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, COLORONCOLOR), COLORONCOLOR);

    T_CASE("a handle that is no context returns 0 with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, (HDC)0xdeadbeef, COLORONCOLOR), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, SetStretchBltMode, dc, COLORONCOLOR), COLORONCOLOR);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_EQ(rt_dc_delete(dc), TRUE);
}
