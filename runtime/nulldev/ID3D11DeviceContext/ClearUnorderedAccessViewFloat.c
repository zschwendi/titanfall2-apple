/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewfloat
 * RT-CASES:  with no back end a clear is accepted and nothing happens; with a back end it is handed on once, the view as its back object and the four values by pointer
 * RT-TRAPS:  ClearUnorderedAccessViewFloat of a NULL view; ClearUnorderedAccessViewFloat with NULL values; rt_nulldev_arg: "is an object of class" ... "where" ... "is expected" (a view of the wrong class)
 */
#include "rt/nulldev.h"

/* A clear of an unordered-access view to floats: the third worked example of the back-end rows
 * (unit back-clear-dsv-uav; rt/d3dback.h).  The view must be a live unordered-access view of
 * this layer (rt_nulldev_arg) and not NULL; the values must not be NULL.  The values cross by
 * pointer, unchanged: the body does not read them. */
COM_METHOD(ID3D11DeviceContext, void, ClearUnorderedAccessViewFloat, ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT *Values)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pUnorderedAccessView) RT_UNIMPLEMENTED(ctx, "ClearUnorderedAccessViewFloat of a NULL view");
    if (!Values) RT_UNIMPLEMENTED(ctx, "ClearUnorderedAccessViewFloat with NULL values");
    (void)rt_nulldev_arg(ctx, pUnorderedAccessView, &rt_nulldev_uav_class, "pUnorderedAccessView");
    RT_BACK(ctx, ClearUnorderedAccessViewFloat, c->child.back, rt_d3dback_of(pUnorderedAccessView), Values);
}
