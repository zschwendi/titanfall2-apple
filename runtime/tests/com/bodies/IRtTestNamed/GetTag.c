/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  the tag of a shape
 * RT-TRAPS:  none
 */
#include "rttest_com.h"

COM_METHOD(IRtTestNamed, UINT, GetTag)
{
    RtTestShapeState *s = rt_com_state(ctx, This, &rttest_shape_class);
    return s->tag;
}
