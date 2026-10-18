/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  eight arguments of every class
 * RT-TRAPS:  none
 */
#include "rttest_com.h"

COM_METHOD(IRtTestShape, HRESULT, Mix, UINT a, FLOAT b, UINT64 c, double d, UINT8 e, const char *text, UINT *out)
{
    RtTestShapeState *s = rt_com_state(ctx, This, &rttest_shape_class);
    *out = a + (UINT)b + (UINT)c + (UINT)d + e + (UINT)strlen(text) + s->tag;
    return (HRESULT)(c >> 32);
}
