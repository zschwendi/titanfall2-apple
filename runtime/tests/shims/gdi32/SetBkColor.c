#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* Expected values: the comments of rt_dc_create_compatible and rt_dc_attr in rt/gdi.h (a new
 * memory context's background is 0xFFFFFF, Wine gdi32/tests/dc.c; a colour is stored as
 * given; a handle that is no context gives ERROR_INVALID_HANDLE). */
TEST(gdi32, SetBkColor)
{
    HDC dc = rt_dc_create_compatible(NULL);
    T_REQUIRE(dc != NULL);

    T_CASE("sets the background colour of a context and returns the one before");
    T_EQ(CALL(gdi32, SetBkColor, dc, 0x00FF0000), 0x00FFFFFF);
    T_EQ(CALL(gdi32, SetBkColor, dc, 0x000000FF), 0x00FF0000);

    T_CASE("a colour is stored as given");
    T_EQ(CALL(gdi32, SetBkColor, dc, 0x01020304), 0x000000FF);
    T_EQ(CALL(gdi32, SetBkColor, dc, 0x00FFFFFF), 0x01020304);

    T_CASE("a handle that is no context gives CLR_INVALID with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(gdi32, SetBkColor, NULL, 0x00FF0000), CLR_INVALID);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(gdi32, SetBkColor, (HDC)(uintptr_t)0x1234, 0x00FF0000), CLR_INVALID);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, SetBkColor, dc, 0x00FF0000), 0x00FFFFFF);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
