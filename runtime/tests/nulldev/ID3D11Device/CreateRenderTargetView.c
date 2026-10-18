/* Expected values: Microsoft's page of the creator (a NULL description stands for a view of
 * the resource in the format it was created with; a NULL out-pointer validates and returns
 * S_FALSE).  That the view holds its resource and the device is the object model's
 * (rt/nulldev.h); the counts follow from it (a new device has 2 references).  The dimension
 * numbers are the enumerations of rt/win/d3d11view.h (checked against the MinGW headers). */
/* Unit d3d-fix-2 (F1 of docs/reviews/d3d-contract-check-2.md): the cases named "F1" assert what
 * Windows reads back from a view, as that block reads Wine's dlls/d3d11/tests/d3d11.c
 * (test_create_rendertarget_view, test_create_shader_resource_view,
 * test_create_depthstencil_view): the numbers 10 mips, 4 slices, "~0u from slice 1 of 4 reads
 * back 3", "~0u from mip 3 reads back 7", the cube counts 6, 11, 12, 14, 18 are that block's;
 * 256 by 256 with 9 mips and "from mip 2 reads back 7" is the block's own test. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateRenderTargetView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *tex = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &tex), S_OK);
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0 };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buf), S_OK);
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture3D *vol = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &vd, NULL, &vol), S_OK);
    LONG dev0 = rt_com_refs(device);
    D3D11_RENDER_TARGET_VIEW_DESC d, bad;
    ID3D11RenderTargetView *view = NULL, *whole = NULL;
    memset(&d, 0, sizeof d);
    /* as the game makes it on its back buffer: the SRGB format, TEXTURE2D, mip 0 (the reference trace, as the orchestrator reports it) */
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice = 0;

    T_CASE("a view with a description: S_OK, the description kept, the device and the resource held");
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)tex, &d, &view), S_OK);
    T_REQUIRE(view != NULL);
    const NdView *v = rt_com_state_of(view, &rt_nulldev_rtv_class);
    T_TRUE(!memcmp(&v->desc.rtv, &d, sizeof d));
    T_TRUE(v->resource == (ID3D11Resource *)tex);
    T_TRUE(v->child.device == device);
    T_EQ(rt_com_refs(view), 1);
    T_EQ(rt_com_refs(tex), 2);
    T_EQ(rt_com_refs(device), dev0 + 1);

    T_CASE("a NULL description on a texture 2D: its format, TEXTURE2D, mip level 0");
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)tex, NULL, &whole), S_OK);
    const NdView *n = rt_com_state_of(whole, &rt_nulldev_rtv_class);
    T_EQ(n->desc.rtv.Format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    T_EQ(n->desc.rtv.ViewDimension, 4);                 /* D3D11_RTV_DIMENSION_TEXTURE2D */
    T_EQ(n->desc.rtv.Texture2D.MipSlice, 0);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    LONG before = rt_com_refs(device), tex_before = rt_com_refs(tex);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), before);
    T_EQ(rt_com_refs(tex), tex_before);

    T_CASE("F1: 256 by 256 with 9 mips and NULL reads back format 28, TEXTURE2D, MipSlice 0");
    D3D11_TEXTURE2D_DESC f1d = { 256, 256, 9, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *f1t = NULL, *f1a = NULL;
    ID3D11RenderTargetView *f1v = NULL;
    D3D11_RENDER_TARGET_VIEW_DESC got;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1t), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)f1t, NULL, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11RenderTargetView, GetDesc, f1v, &got);
    T_EQ(got.Format, 28);
    T_EQ(got.ViewDimension, 4);
    T_EQ(got.Texture2D.MipSlice, 0);

    T_CASE("F1: a 4-slice texture and NULL: TEXTURE2DARRAY, MipSlice 0, first slice 0, 4 slices");
    f1d.MipLevels = 1; f1d.ArraySize = 4;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1a), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)f1a, NULL, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11RenderTargetView, GetDesc, f1v, &got);
    T_EQ(got.Format, 28);
    T_EQ(got.ViewDimension, 5);                         /* D3D11_RTV_DIMENSION_TEXTURE2DARRAY */
    T_EQ(got.Texture2DArray.MipSlice, 0);
    T_EQ(got.Texture2DArray.FirstArraySlice, 0);
    T_EQ(got.Texture2DArray.ArraySize, 4);

    T_CASE("F1: format UNKNOWN reads back the texture format, and ArraySize 0xffffffff from slice 1 of 4 reads back 3");
    D3D11_RENDER_TARGET_VIEW_DESC rest;
    memset(&rest, 0, sizeof rest);
    rest.Format = DXGI_FORMAT_UNKNOWN; rest.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rest.Texture2DArray.FirstArraySlice = 1; rest.Texture2DArray.ArraySize = 0xffffffffu;
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)f1a, &rest, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11RenderTargetView, GetDesc, f1v, &got);
    T_EQ(got.Format, 28);
    T_EQ(got.ViewDimension, 5);
    T_EQ(got.Texture2DArray.FirstArraySlice, 1);
    T_EQ(got.Texture2DArray.ArraySize, 3);

    T_TRAPS(COM_CALL(ID3D11Device, CreateRenderTargetView, device, NULL, &d, &view), "a view on a NULL resource");
    bad = d; bad.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
    T_TRAPS(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)tex, &bad, &view), "which is not one of a texture 2D's");
    D3D11_TEXTURE2D_DESC ms = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture2D *msaa = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &msaa), S_OK);
    T_TRAPS(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)msaa, NULL, &view), "a NULL description on a texture 2D that is not a plain texture");
    (void)buf; (void)vol;
}
