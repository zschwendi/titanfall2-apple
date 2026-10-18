/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a float in, a float out
 * RT-TRAPS:  none
 */
#include "rttest_com.h"

COM_METHOD(IRtTestShape, FLOAT, Scale, FLOAT x)
{
    RtTestShapeState *s = rt_com_state(ctx, This, &rttest_shape_class);
    return x * (FLOAT)s->tag;
}
