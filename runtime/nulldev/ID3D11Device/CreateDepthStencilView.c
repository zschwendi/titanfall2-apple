/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilview
 * RT-CASES:  a view with a description: S_OK, the description kept, the device and the resource held; a NULL description on a texture 2D: its format, TEXTURE2D, no flags, mip level 0; a NULL out-pointer validates only: S_FALSE and no object; F1: a 4-slice depth texture and NULL: its format, TEXTURE2DARRAY, no flags, mip level 0, 4 slices from 0; F1: ArraySize 0xffffffff from slice 1 of 4 reads back 3, the read-only flags kept
 * RT-TRAPS:  a view on a NULL resource; which is not one of a texture 3D's; a NULL description on a buffer; a NULL description on a resource of a typeless format
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text), pDesc:
 * "Set this parameter to NULL to create a view that accesses mipmap level 0 of the entire resource (using the format
 * the resource was created with)."
 * The out-pointer: "Set this parameter to NULL to validate the other input parameters (the method
 * will return S_FALSE if the other input parameters pass validation)."  S_OK for success is
 * from memory (the page says "one of the Direct3D 11 Return Codes").  The view holds its
 * resource (GetResource hands it out; that a view keeps its resource alive is from memory).
 * The record, what a NULL description is filled in with and every stop:
 * rt_nulldev_view_create (rt/nulldev.h; runtime/nulldev/creators.c). */
COM_METHOD(ID3D11Device, HRESULT, CreateDepthStencilView, ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView)
{
    HRESULT hr = rt_nulldev_view_create(ctx, This, &rt_nulldev_dsv_class, pResource, pDesc, (void **)ppDepthStencilView);
    /* The seam (rt/d3dback.h, rules 2 and 3): the resource crosses as its back object, the
     * description as the game passed it (NULL as NULL).  No view when the call only validated. */
    RT_BACK_CREATE(ctx, ppDepthStencilView ? *ppDepthStencilView : NULL, CreateDepthStencilView, rt_d3dback_of(This), rt_d3dback_of(pResource), pDesc);
    return hr;
}
