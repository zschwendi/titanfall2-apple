/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createshaderresourceview
 * RT-CASES:  a view with a description: S_OK, the description kept, the device and the resource held; a NULL description on a texture 2D: its format, TEXTURE2D, every mip level; a NULL out-pointer validates only: S_FALSE and no object; the view keeps the resource alive after the game releases it; F1: 256 by 256 with 9 mips and NULL reads back format 28, TEXTURE2D, most detailed 0, 9 levels; F1: format 0 and MipLevels 0xffffffff from mip 2 of 9 read back format 28 and 7 levels; F1: NULL on a texture with the cube flag: TEXTURECUBE for 6 and 11 slices, a cube array of 2 for 12 and 14 and of 3 for 18; F1: NULL on a 4-slice texture: TEXTURE2DARRAY, every mip level, 4 slices from 0; a view of a structured buffer keeps format UNKNOWN and its element range
 * RT-TRAPS:  a view on a NULL resource; which is not one of a buffer's; a NULL description on a buffer; which has no size in the format table; where an object of class D3D11Device is expected
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
COM_METHOD(ID3D11Device, HRESULT, CreateShaderResourceView, ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView)
{
    HRESULT hr = rt_nulldev_view_create(ctx, This, &rt_nulldev_srv_class, pResource, pDesc, (void **)ppSRView);
    /* The seam (rt/d3dback.h, rules 2 and 3): the resource crosses as its back object, the
     * description as the game passed it (NULL as NULL).  No view when the call only validated. */
    RT_BACK_CREATE(ctx, ppSRView ? *ppSRView : NULL, CreateShaderResourceView, rt_d3dback_of(This), rt_d3dback_of(pResource), pDesc);
    return hr;
}
