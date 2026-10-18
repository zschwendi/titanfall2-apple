/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  the value the test put in the state of a root object
 * RT-TRAPS:  none
 */
#include "rttest_com.h"

COM_METHOD(IRtTestRoot, UINT, Value)
{
    RtTestRootState *s = rt_com_state(ctx, This, &rttest_root_class);
    return s->value;
}
