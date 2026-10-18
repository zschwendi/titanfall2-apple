/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createunorderedaccessview
 * RT-CASES:  a view with a description: S_OK, the description kept, the device and the resource held; a NULL description on a texture 2D: its format, TEXTURE2D, mip level 0; a NULL out-pointer validates only: S_FALSE and no object; F1: one slice of an 80-slice array as the game makes it is kept as passed, and ArraySize 0xffffffff from slice 77 reads back 3; F1: a 4-slice texture and NULL: TEXTURE2DARRAY, mip level 0, 4 slices from 0
 * RT-TRAPS:  a view on a NULL resource; which is not one of a texture 2D's; a NULL description on a texture 3D
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text), pDesc:
 * "Set this parameter to NULL to create a view that accesses the entire resource (using the format the resource was
 * created with)."
 * The out-pointer: "Set this parameter to NULL to validate the other input parameters (the method
 * will return S_FALSE if the other input parameters pass validation)."  S_OK for success is
 * from memory (the page says "one of the Direct3D 11 Return Codes").  The view holds its
 * resource (GetResource hands it out; that a view keeps its resource alive is from memory).
 * The record, what a NULL description is filled in with and every stop:
 * rt_nulldev_view_create (rt/nulldev.h; runtime/nulldev/creators.c). */
COM_METHOD(ID3D11Device, HRESULT, CreateUnorderedAccessView, ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView)
{
    HRESULT hr = rt_nulldev_view_create(ctx, This, &rt_nulldev_uav_class, pResource, pDesc, (void **)ppUAView);
    /* The seam (rt/d3dback.h, rules 2 and 3): the resource crosses as its back object, the
     * description as the game passed it (NULL as NULL).  No view when the call only validated. */
    RT_BACK_CREATE(ctx, ppUAView ? *ppUAView : NULL, CreateUnorderedAccessView, rt_d3dback_of(This), rt_d3dback_of(pResource), pDesc);
    return hr;
}
