/* The views Windows refuses (unit d3d-fix-3; finding 2 of docs/reviews/
 * night-2026-10-11-code-review.md, G6 of docs/reviews/d3d-code-review.md).
 *
 * Expected values.  That each of these is refused: Microsoft's pages as creators.c quotes them
 * above view_problem (the bind flag for three of the four kinds, no typeless format, the
 * depth-stencil formats, MostDetailedMip below the texture's levels), and from memory where
 * that comment says so (the unordered-access view's flag, a typed resource's own format, array
 * and depth ranges).  The device stops where Windows answers E_INVALIDARG.  That the views of
 * the second half are made: the recording, docs/trace/d3d-creators.md, section "Views", whose
 * rows they are (S_OK on the owner's machine).  The first case is the review's sequence: it
 * returned S_OK and then the stale bytes X. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

#define TEX(var, w, h, mips, array, format, usage, bind, cpu, misc) \
    ID3D11Texture2D *var = NULL; \
    do { D3D11_TEXTURE2D_DESC td_ = { (w), (h), (mips), (array), (format), { 1, 0 }, (usage), (bind), (cpu), (misc) }; \
         T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td_, NULL, &var), S_OK); } while (0)
#define SRV(res, desc, out) COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)(res), (desc), (out))
#define RTV(res, desc, out) COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)(res), (desc), (out))
#define DSV(res, desc, out) COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)(res), (desc), (out))
#define UAV(res, desc, out) COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)(res), (desc), (out))

TEST(core, nulldev_view_refusals)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11UnorderedAccessView *uav = NULL;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd;
    D3D11_RENDER_TARGET_VIEW_DESC rd;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;

    T_CASE("a render-target view of a staging texture is refused, so no renderer can write what a read mapping hands out");
    static uint8_t x[16 * 16 * 4];
    memset(x, 0x58, sizeof x);
    D3D11_SUBRESOURCE_DATA init = { x, 64, 0 };
    D3D11_TEXTURE2D_DESC st = { 16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *staging = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &st, &init, &staging), S_OK);
    T_TRAPS(RTV(staging, NULL, &rtv), "its resource was created without the bind flag 0x20 of this kind of view");
    T_TRUE(rtv == NULL);
    T_EQ(rt_nulldev_content(staging, 0), ND_CONTENT_GAME);

    T_CASE("each kind of view needs its bind flag on the resource");
    D3D11_BUFFER_DESC sbd = { 64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *sbuf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &sbd, NULL, &sbuf), S_OK);
    memset(&ud, 0, sizeof ud);
    ud.Format = DXGI_FORMAT_R32_UINT; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 16;
    T_TRAPS(UAV(sbuf, &ud, &uav), "without the bind flag 0x80");
    TEX(only_rt, 64, 32, 3, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0);
    TEX(only_sr, 64, 32, 3, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0);
    T_TRAPS(SRV(only_rt, NULL, &srv), "without the bind flag 0x8 ");
    T_TRAPS(RTV(only_sr, NULL, &rtv), "without the bind flag 0x20");
    T_TRAPS(DSV(only_sr, NULL, &dsv), "without the bind flag 0x40");
    T_TRAPS(UAV(only_sr, NULL, &uav), "without the bind flag 0x80");
    T_EQ(RTV(only_rt, NULL, &rtv), S_OK);
    T_EQ(SRV(only_sr, NULL, &srv), S_OK);

    T_CASE("a view's mip levels lie inside the texture's");
    memset(&sd, 0, sizeof sd);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 2; sd.Texture2D.MipLevels = 2;
    T_TRAPS(SRV(only_sr, &sd, &srv), "mip levels 2 to 4, of a texture of 3");
    sd.Texture2D.MostDetailedMip = 3; sd.Texture2D.MipLevels = ~0u;
    T_TRAPS(SRV(only_sr, &sd, &srv), "of a texture of 3");
    sd.Texture2D.MostDetailedMip = 0; sd.Texture2D.MipLevels = 0;
    T_TRAPS(SRV(only_sr, &sd, &srv), "mip levels 0 to 0");
    sd.Format = DXGI_FORMAT_UNKNOWN; sd.Texture2D.MostDetailedMip = 1; sd.Texture2D.MipLevels = ~0u;
    T_EQ(SRV(only_sr, &sd, &srv), S_OK);                /* G6's own case: kept as format 28 and 2 levels */
    const NdView *kept = rt_com_state_of(srv, &rt_nulldev_srv_class);
    T_EQ(kept->desc.srv.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
    T_EQ(kept->desc.srv.Texture2D.MipLevels, 2);
    memset(&rd, 0, sizeof rd);
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rd.Texture2D.MipSlice = 3;
    T_TRAPS(RTV(only_rt, &rd, &rtv), "mip levels 3 to 4, of a texture of 3");
    rd.Texture2D.MipSlice = 2;
    T_EQ(RTV(only_rt, &rd, &rtv), S_OK);

    T_CASE("a view's format is typed and of its resource's family");
    TEX(typeless, 32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0);
    T_TRAPS(SRV(typeless, NULL, &srv), "a NULL description on a resource of a typeless format");
    T_TRAPS(RTV(typeless, NULL, &rtv), "a NULL description on a resource of a typeless format");
    sd.Format = DXGI_FORMAT_UNKNOWN; sd.Texture2D.MostDetailedMip = 0; sd.Texture2D.MipLevels = 1;
    T_TRAPS(SRV(typeless, &sd, &srv), "the resource's own typeless format");
    sd.Format = DXGI_FORMAT_R16G16B16A16_TYPELESS;
    T_TRAPS(SRV(typeless, &sd, &srv), "a typeless format, which no view can use");
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    T_TRAPS(SRV(typeless, &sd, &srv), "a format that is not of the family of its resource's typeless format");
    rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.Texture2D.MipSlice = 0;
    T_TRAPS(RTV(typeless, &rd, &rtv), "not of the family");
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    T_TRAPS(SRV(only_sr, &sd, &srv), "a format that is not the typed format its resource was created with");
    T_EQ(SRV(typeless, &sd, &srv), S_OK);               /* the recording: 88 such views */
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    T_EQ(RTV(typeless, &rd, &rtv), S_OK);               /* the recording: 3 */

    T_CASE("a depth-stencil view has one of the four depth formats");
    TEX(depth32, 32, 32, 1, 1, DXGI_FORMAT_R32_TYPELESS, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0);
    TEX(depth24, 32, 32, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0);
    memset(&dd, 0, sizeof dd);
    dd.Format = DXGI_FORMAT_R32_FLOAT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    T_TRAPS(DSV(depth32, &dd, &dsv), "a format that is not one of the four depth formats");
    T_TRAPS(DSV(depth32, NULL, &dsv), "a NULL description on a resource of a typeless format");
    dd.Format = DXGI_FORMAT_D32_FLOAT; dd.Flags = D3D11_DSV_READ_ONLY_DEPTH;
    T_EQ(DSV(depth32, &dd, &dsv), S_OK);                /* the recording's row at call 7067 */
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.Flags = 0;
    T_EQ(DSV(depth24, &dd, &dsv), S_OK);                /* 5412 */
    T_TRAPS(DSV(depth32, &dd, &dsv), "not of the family");
    sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    T_EQ(SRV(depth24, &sd, &srv), S_OK);                /* 5411: a view format with _TYPELESS in its name that Windows takes */

    T_CASE("a view's slices lie inside the texture's array, cubes or depth");
    TEX(layers, 16, 16, 1, 80, DXGI_FORMAT_R32G32_TYPELESS, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, 0);
    memset(&ud, 0, sizeof ud);
    ud.Format = DXGI_FORMAT_R32G32_FLOAT; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    ud.Texture2DArray.FirstArraySlice = 79; ud.Texture2DArray.ArraySize = 1;
    T_EQ(UAV(layers, &ud, &uav), S_OK);                 /* the recording's row at 89618, its last slice */
    ud.Texture2DArray.ArraySize = 2;
    T_TRAPS(UAV(layers, &ud, &uav), "slices 79 to 81, of 80");
    ud.Texture2DArray.FirstArraySlice = 80; ud.Texture2DArray.ArraySize = ~0u;
    T_TRAPS(UAV(layers, &ud, &uav), "of 80");
    ud.Texture2DArray.FirstArraySlice = 0; ud.Texture2DArray.ArraySize = 0;
    T_TRAPS(UAV(layers, &ud, &uav), "slices 0 to 0, of 80");
    TEX(cube, 32, 32, 6, 6, DXGI_FORMAT_R16G16B16A16_TYPELESS, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_TEXTURECUBE);
    memset(&sd, 0, sizeof sd);
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    sd.TextureCubeArray.MipLevels = 6; sd.TextureCubeArray.NumCubes = 1;
    T_EQ(SRV(cube, &sd, &srv), S_OK);                   /* 145173 */
    sd.TextureCubeArray.NumCubes = 2;
    T_TRAPS(SRV(cube, &sd, &srv), "2 cubes, of a texture of 6 slices");
    sd.TextureCubeArray.NumCubes = 1; sd.TextureCubeArray.First2DArrayFace = 1;
    T_TRAPS(SRV(cube, &sd, &srv), "slices 1 to 7, of 6");
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 2, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture3D *vol = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &vd, NULL, &vol), S_OK);
    memset(&rd, 0, sizeof rd);
    rd.Format = DXGI_FORMAT_R8_UNORM; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
    rd.Texture3D.MipSlice = 1; rd.Texture3D.FirstWSlice = 2; rd.Texture3D.WSize = 2;
    T_EQ(RTV(vol, &rd, &rtv), S_OK);                    /* level 1 is 4 deep */
    rd.Texture3D.WSize = 3;
    T_TRAPS(RTV(vol, &rd, &rtv), "slices 2 to 5, of 4");
}
