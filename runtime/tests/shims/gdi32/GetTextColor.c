#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* Expected values: the comments of rt_dc_create_compatible and rt_dc_attr in rt/gdi.h (a handle
 * that is no context gives CLR_INVALID and ERROR_INVALID_HANDLE); the first get of a new
 * context's text colour is 0 (planner's choice). */
TEST(gdi32, GetTextColor)
{
    HDC dc = rt_dc_create_compatible(NULL);
    T_REQUIRE(dc != NULL);

    T_CASE("returns the text colour of a context");
    T_EQ(CALL(gdi32, GetTextColor, dc), 0x00000000);
    CALL(gdi32, SetTextColor, dc, 0x00FF0000);
    T_EQ(CALL(gdi32, GetTextColor, dc), 0x00FF0000);

    T_CASE("a handle that is no context gives CLR_INVALID with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(gdi32, GetTextColor, NULL), CLR_INVALID);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(gdi32, GetTextColor, (HDC)(uintptr_t)0x1234), CLR_INVALID);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, GetTextColor, dc), 0x00FF0000);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
