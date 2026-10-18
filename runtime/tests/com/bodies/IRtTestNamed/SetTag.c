/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  a tag below 0x1000
 * RT-TRAPS:  a tag of 0x1000 or more
 */
#include "rttest_com.h"

COM_METHOD(IRtTestNamed, HRESULT, SetTag, UINT tag)
{
    RtTestShapeState *s = rt_com_state(ctx, This, &rttest_shape_class);
    if (tag >= 0x1000) RT_UNIMPLEMENTED(ctx, "SetTag with tag %#x", tag);
    s->tag = tag;
    return S_OK;
}
