#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core: rt_dc_set_point with RT_DC_VIEWPORT_ORG reads the viewport origin,
 * which this call must not change.  A memory context starts with current position {0, 0}. */
TEST(gdi32, MoveToEx)
{
    HDC m;
    POINT p, q, org;

    m = rt_dc_create_compatible(NULL);
    T_REQUIRE(m != NULL);

    T_CASE("stores the current position and returns the previous one in the POINT");
    p.x = -1;
    p.y = -1;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, MoveToEx, m, 5, -7, &p), TRUE);
    T_EQ(p.x, 0);
    T_EQ(p.y, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* success leaves last error alone */
    q.x = 99;
    q.y = 99;
    T_EQ(CALL(gdi32, MoveToEx, m, 1, 2, &q), TRUE);
    T_EQ(q.x, 5);
    T_EQ(q.y, -7);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a NULL POINT is allowed");
    T_EQ(CALL(gdi32, MoveToEx, m, 3, 3, NULL), TRUE);
    p.x = -1;
    p.y = -1;
    T_EQ(CALL(gdi32, MoveToEx, m, 0, 0, &p), TRUE);
    T_EQ(p.x, 3);
    T_EQ(p.y, 3);

    T_CASE("the current position and the viewport origin are independent");
    org.x = 0;
    org.y = 0;
    T_EQ(rt_dc_set_point(m, RT_DC_VIEWPORT_ORG, -8, 9, &org), TRUE);
    T_EQ(CALL(gdi32, MoveToEx, m, 40, 50, &p), TRUE);
    T_EQ(p.x, 0);
    T_EQ(p.y, 0);
    T_EQ(rt_dc_set_point(m, RT_DC_VIEWPORT_ORG, 0, 0, &org), TRUE);
    T_EQ(org.x, -8);
    T_EQ(org.y, 9);

    T_CASE("a handle that is no context fails");
    p.x = -3;
    p.y = -4;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, MoveToEx, (HDC)0x40000FF0, 1, 2, &p), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(p.x, -3);                                /* the POINT is left alone */
    T_EQ(p.y, -4);
    T_EQ(CALL(gdi32, MoveToEx, NULL, 1, 2, &p), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(p.x, -3);
    T_EQ(p.y, -4);
    T_EQ(CALL(gdi32, MoveToEx, (HDC)0x40000FF0, 1, 2, NULL), FALSE);
    T_EQ(CALL(gdi32, MoveToEx, m, 0, 0, &q), TRUE);  /* the failures changed nothing */
    T_EQ(q.x, 40);
    T_EQ(q.y, 50);
}
