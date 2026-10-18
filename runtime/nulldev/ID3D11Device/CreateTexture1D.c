/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture1d
 * RT-CASES:  width 32 of R8_UNORM with MipLevels 0: 6 levels in the description, 6 subresources, 63 bytes; initial data is copied as it lies; a NULL out-pointer validates only: S_FALSE and no object left
 * RT-TRAPS:  CreateTexture1D with a NULL description; a texture 1D description with a size of 0
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): pInitialData is "an array of
 * D3D11_SUBRESOURCE_DATA structures that describe subresources", as many as "MipLevels * ArraySize"; ppTexture1D: "Set
 * this parameter to NULL to validate the other input parameters (the method will return S_FALSE
 * if the other input parameters pass validation)"; "If the method succeeds, the return code is
 * S_OK."  What the record keeps, how the subresources are sized and every stop of a
 * description: rt_nulldev_texture_new and rt_nulldev_fill (rt/nulldev.h; memory.c has the
 * sources).  The page names no result for a NULL description: a stop. */
COM_METHOD(ID3D11Device, HRESULT, CreateTexture1D, const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "CreateTexture1D with a NULL description");
    ID3D11Texture1D *texture = rt_nulldev_texture_new(ctx, This, D3D11_RESOURCE_DIMENSION_TEXTURE1D, pDesc, pInitialData != NULL);
    if (pInitialData) rt_nulldev_fill(ctx, texture, pInitialData);
    if (!ppTexture1D) {
        rt_com_release(texture);
        RT_BACK_ALONE(ctx, CreateTexture1D);    /* the call only validated: no object, so the back end is not asked */
        return S_FALSE;
    }
    /* The seam (rt/d3dback.h, rules 3 and 4): the front has its object and its answer; the back
     * end makes its own from the game's description and data, unchanged. */
    RT_BACK_CREATE(ctx, texture, CreateTexture1D, rt_d3dback_of(This), pDesc, pInitialData);
    *ppTexture1D = texture;
    return S_OK;
}
