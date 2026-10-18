#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core's record: rt_gdi_get_object reads back what the pen was made from.
 * Handles of objects are at or above 0x40000000 (the core's handle space). */
#define OBJ_MIN 0x40000000u

TEST(gdi32, CreatePen)
{
    HPEN p, q;
    LOGPEN lp;

    T_CASE("creates a pen of style, width and colour");
    p = CALL(gdi32, CreatePen, 0, 3, 0x123456);
    T_TRUE(p != NULL && (uintptr_t)p >= OBJ_MIN);
    memset(&lp, 0xAA, sizeof lp);
    T_EQ(rt_gdi_get_object(p, 16, &lp, false), 16);
    T_EQ(lp.lopnStyle, PS_SOLID);
    T_EQ(lp.lopnWidth.x, 3);
    T_EQ(lp.lopnWidth.y, 0);
    T_EQ(lp.lopnColor, 0x123456u);
    T_EQ(rt_gdi_delete(p), TRUE);

    T_CASE("a negative width reads back as its absolute value");
    p = CALL(gdi32, CreatePen, 0, -3, 0x123456);
    T_TRUE(p != NULL && (uintptr_t)p >= OBJ_MIN);
    memset(&lp, 0xAA, sizeof lp);
    T_EQ(rt_gdi_get_object(p, 16, &lp, false), 16);
    T_EQ(lp.lopnWidth.x, 3);
    T_EQ(lp.lopnWidth.y, 0);
    T_EQ(lp.lopnColor, 0x123456u);
    T_EQ(rt_gdi_delete(p), TRUE);

    T_CASE("PS_NULL pen reads back as the null pen");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(gdi32, CreatePen, 5, 7, 0x123456);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* success leaves last error alone */
    T_TRUE(p != NULL && (uintptr_t)p >= OBJ_MIN);
    memset(&lp, 0xAA, sizeof lp);
    T_EQ(rt_gdi_get_object(p, 16, &lp, false), 16);
    T_EQ(lp.lopnStyle, PS_NULL);
    T_EQ(lp.lopnWidth.x, 1);
    T_EQ(lp.lopnWidth.y, 0);
    T_EQ(lp.lopnColor, 0u);
    T_EQ(rt_gdi_delete(p), TRUE);

    T_CASE("each call gives a new handle");
    p = CALL(gdi32, CreatePen, 0, 1, 0);
    q = CALL(gdi32, CreatePen, 0, 1, 0);
    T_TRUE(p != NULL && q != NULL && p != q);
    T_EQ(rt_gdi_delete(p), TRUE);
    T_EQ(rt_gdi_delete(q), TRUE);

    /* RT-TRAPS: a pen style outside 0 to 6 is a stop of the core (rt_gdi_create_pen). */
    T_TRAPS(CALL(gdi32, CreatePen, 9, 1, 0), "rt_gdi_create_pen: not implemented");
}
