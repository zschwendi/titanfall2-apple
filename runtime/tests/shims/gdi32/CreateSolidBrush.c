#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core's record: rt_gdi_get_object reads back the brush's style, colour and
 * hatch.  Handles of objects are at or above 0x40000000 (the core's handle space). */
#define OBJ_MIN 0x40000000u

TEST(gdi32, CreateSolidBrush)
{
    HBRUSH b, c;
    LOGBRUSH lb;

    T_CASE("creates a solid brush of the colour given");
    b = CALL(gdi32, CreateSolidBrush, 0x00030201u);
    T_TRUE(b != NULL && (uintptr_t)b >= OBJ_MIN);
    memset(&lb, 0xAA, sizeof lb);
    T_EQ(rt_gdi_get_object(b, 16, &lb, false), 16);
    T_EQ(lb.lbStyle, BS_SOLID);
    T_EQ(lb.lbColor, 0x00030201u);
    T_EQ(lb.lbHatch, 0u);
    T_EQ(rt_gdi_delete(b), TRUE);

    T_CASE("each call gives a new handle");
    b = CALL(gdi32, CreateSolidBrush, 0);
    c = CALL(gdi32, CreateSolidBrush, 0);
    T_TRUE(b != NULL && c != NULL && b != c);
    T_EQ(rt_gdi_delete(b), TRUE);
    T_EQ(rt_gdi_delete(c), TRUE);
}
