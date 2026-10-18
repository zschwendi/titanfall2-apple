/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11texture2d-getdesc
 * RT-CASES:  the description the texture was created with, all 44 bytes, and nothing after them
 * RT-TRAPS:  GetDesc with a NULL description
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Get the properties of the texture resource."  The description is the
 * one the texture was created with, kept in the record (NdTexture.desc.d2, MipLevels already the
 * real count).  The method returns nothing.  The page names no result for a NULL pDesc: a stop. */
COM_METHOD(ID3D11Texture2D, void, GetDesc, D3D11_TEXTURE2D_DESC *pDesc)
{
    NdTexture *t = rt_com_state(ctx, This, &rt_nulldev_texture2d_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "GetDesc with a NULL description");
    *pDesc = t->desc.d2;
}
