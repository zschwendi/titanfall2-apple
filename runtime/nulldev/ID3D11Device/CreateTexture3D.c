/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture3d
 * RT-CASES:  32 by 32 by 4 of R8_UNORM with MipLevels 0: 6 levels in the description, 6 subresources, 4693 bytes; initial data is copied with its rows and slices apart; a NULL out-pointer validates only: S_FALSE and no object left
 * RT-TRAPS:  CreateTexture3D with a NULL description; a texture 3D description with a size of 0; smaller than a slice of 2 rows
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): pInitialData is "an array of
 * D3D11_SUBRESOURCE_DATA structures that describe subresources", one per mip level ("Each element of pInitialData provides all of the slices that are defined for a given miplevel"); ppTexture3D: "Set
 * this parameter to NULL to validate the other input parameters (the method will return S_FALSE
 * if the other input parameters pass validation)"; "If the method succeeds, the return code is
 * S_OK."  What the record keeps, how the subresources are sized and every stop of a
 * description: rt_nulldev_texture_new and rt_nulldev_fill (rt/nulldev.h; memory.c has the
 * sources).  The page names no result for a NULL description: a stop. */
COM_METHOD(ID3D11Device, HRESULT, CreateTexture3D, const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "CreateTexture3D with a NULL description");
    ID3D11Texture3D *texture = rt_nulldev_texture_new(ctx, This, D3D11_RESOURCE_DIMENSION_TEXTURE3D, pDesc, pInitialData != NULL);
    if (pInitialData) rt_nulldev_fill(ctx, texture, pInitialData);
    if (!ppTexture3D) {
        rt_com_release(texture);
        RT_BACK_ALONE(ctx, CreateTexture3D);       /* the call only validated: no object, so the back end is not asked */
        return S_FALSE;
    }
    RT_BACK_CREATE(ctx, texture, CreateTexture3D, rt_d3dback_of(This), pDesc, pInitialData);
    *ppTexture3D = texture;
    return S_OK;
}
