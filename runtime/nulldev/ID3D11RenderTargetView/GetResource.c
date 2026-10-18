/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11view-getresource
 * RT-CASES:  a view with a resource gives it, with one more reference for the receiver; a view with no resource gives NULL
 * RT-TRAPS:  GetResource with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "This function increments the reference count of the resource by
 * one, so it is necessary to call Release on the returned pointer when the application is done
 * with it."  The method is ID3D11View's, which the render-target view inherits (the page is the
 * one of ID3D11View, as EnumOutputs' page is the one of IDXGIAdapter).  The resource is the face
 * the creator was given (NdView.resource), or NULL for a view that has none.  The page names no
 * result for a NULL ppResource: a stop. */
COM_METHOD(ID3D11RenderTargetView, void, GetResource, ID3D11Resource **ppResource)
{
    NdView *v = rt_com_state(ctx, This, &rt_nulldev_rtv_class);
    if (!ppResource) RT_UNIMPLEMENTED(ctx, "GetResource with a NULL out-pointer");
    *ppResource = rt_nulldev_ref(v->resource);
}
