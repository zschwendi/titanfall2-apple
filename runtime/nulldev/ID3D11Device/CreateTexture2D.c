/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d
 * RT-CASES:  256 by 256 of BC1_UNORM with MipLevels 0: 9 levels in the description, 9 subresources, 43704 bytes; 6 slices of 4 by 4 R8G8B8A8_UNORM with 3 levels: 18 subresources, 504 bytes; initial data is copied row by row from a pitch larger than the row; a multisampled texture has no memory; a NULL out-pointer validates only: S_FALSE and no object left
 * RT-TRAPS:  CreateTexture2D with a NULL description; a format that has no size in the format table; more mip levels than the full chain; a texture 2D description with a size of 0; CPU read access without staging usage; initial data for a multisampled texture; smaller than a row of 8 bytes
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): pInitialData is "an array of
 * D3D11_SUBRESOURCE_DATA structures that describe subresources", as many as "MipLevels * ArraySize"; "If the resource is multisampled, pInitialData must be NULL"; ppTexture2D: "Set
 * this parameter to NULL to validate the other input parameters (the method will return S_FALSE
 * if the other input parameters pass validation)"; "If the method succeeds, the return code is
 * S_OK."  What the record keeps, how the subresources are sized and every stop of a
 * description: rt_nulldev_texture_new and rt_nulldev_fill (rt/nulldev.h; memory.c has the
 * sources).  The page names no result for a NULL description: a stop. */
COM_METHOD(ID3D11Device, HRESULT, CreateTexture2D, const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "CreateTexture2D with a NULL description");
    ID3D11Texture2D *texture = rt_nulldev_texture_new(ctx, This, D3D11_RESOURCE_DIMENSION_TEXTURE2D, pDesc, pInitialData != NULL);
    if (pInitialData) rt_nulldev_fill(ctx, texture, pInitialData);
    if (!ppTexture2D) {
        rt_com_release(texture);
        RT_BACK_ALONE(ctx, CreateTexture2D);    /* the call only validated: no object, so the back end is not asked */
        return S_FALSE;
    }
    /* The seam (rt/d3dback.h, rules 3 and 4): the front has its object and its answer; the back
     * end makes its own from the game's description and data, unchanged. */
    RT_BACK_CREATE(ctx, texture, CreateTexture2D, rt_d3dback_of(This), pDesc, pInitialData);
    *ppTexture2D = texture;
    return S_OK;
}
