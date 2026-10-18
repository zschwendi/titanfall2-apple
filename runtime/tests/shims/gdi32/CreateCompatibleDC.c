#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core: a memory context is the one rt_dc_window names with -1 (a live
 * context of another kind, rt/gdi.h); a context made from the screen or from a display context
 * is a memory context, and it is not the screen's or the display's own context. */
TEST(gdi32, CreateCompatibleDC)
{
    HDC s = rt_dc_get(NULL);
    HDC m, m2;
    HWND w;
    int kind;

    T_REQUIRE(s != NULL);

    T_CASE("from the screen (NULL) a memory context");
    CALL(kernel32, SetLastError, 0x1234);
    m = CALL(gdi32, CreateCompatibleDC, NULL);
    T_TRUE(m != NULL);
    T_TRUE(m != s);
    T_EQ(rt_dc_window(m, &w, NULL), -1);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* success leaves last error alone */

    T_CASE("from a display context a memory context");
    m2 = CALL(gdi32, CreateCompatibleDC, s);
    T_TRUE(m2 != NULL);
    T_TRUE(m2 != m && m2 != s);
    T_EQ(rt_dc_window(m2, &w, NULL), -1);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no context gives NULL");
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(gdi32, CreateCompatibleDC, (HDC)0x40000FF0) == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* no last error set: from memory, review S26 */
    kind = rt_dc_window((HDC)0x40000FF0, &w, NULL);
    T_EQ(kind, 0);
}
