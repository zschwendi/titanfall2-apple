/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  counts up from 1
 * RT-TRAPS:  none
 */
#include "rttest_com.h"

COM_METHOD(IRtTestCounter, UINT, Next)
{
    RtTestShapeState *s = rt_com_state(ctx, This, &rttest_shape_class);
    return ++s->next;
}
