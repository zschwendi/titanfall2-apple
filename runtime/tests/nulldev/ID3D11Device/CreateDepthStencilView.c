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

TEST(ID3D11Device, CreateDepthStencilView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_R24G8_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
    ID3D11Texture2D *tex = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &tex), S_OK);
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0 };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buf), S_OK);
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture3D *vol = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &vd, NULL, &vol), S_OK);
    LONG dev0 = rt_com_refs(device);
    D3D11_DEPTH_STENCIL_VIEW_DESC d, bad;
    ID3D11DepthStencilView *view = NULL, *whole = NULL;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; d.Flags = 0; d.Texture2D.MipSlice = 1;

    T_CASE("a view with a description: S_OK, the description kept, the device and the resource held");
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, &d, &view), S_OK);
    T_REQUIRE(view != NULL);
    const NdView *v = rt_com_state_of(view, &rt_nulldev_dsv_class);
    T_TRUE(!memcmp(&v->desc.dsv, &d, sizeof d));
    T_TRUE(v->resource == (ID3D11Resource *)tex);
    T_TRUE(v->child.device == device);
    T_EQ(rt_com_refs(view), 1);
    T_EQ(rt_com_refs(tex), 2);
    T_EQ(rt_com_refs(device), dev0 + 1);

    T_CASE("a NULL description on a texture 2D: its format, TEXTURE2D, no flags, mip level 0");
    /* a texture with a typed format: a NULL description stands for the resource's format, and
       "a depth-stencil view cannot use a typeless format" (D3D11_DEPTH_STENCIL_VIEW_DESC; G6) */
    D3D11_TEXTURE2D_DESC typed_desc = td;
    typed_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    ID3D11Texture2D *typed = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &typed_desc, NULL, &typed), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)typed, NULL, &whole), S_OK);
    const NdView *n = rt_com_state_of(whole, &rt_nulldev_dsv_class);
    T_EQ(n->desc.dsv.Format, DXGI_FORMAT_D24_UNORM_S8_UINT);
    T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, NULL, &whole),
            "a NULL description on a resource of a typeless format");
    T_EQ(n->desc.dsv.ViewDimension, 3);                 /* D3D11_DSV_DIMENSION_TEXTURE2D */
    T_EQ(n->desc.dsv.Flags, 0);
    T_EQ(n->desc.dsv.Texture2D.MipSlice, 0);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    LONG before = rt_com_refs(device), tex_before = rt_com_refs(tex);
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), before);
    T_EQ(rt_com_refs(tex), tex_before);

    T_CASE("F1: a 4-slice depth texture and NULL: its format, TEXTURE2DARRAY, no flags, mip level 0, 4 slices from 0");
    D3D11_TEXTURE2D_DESC f1d = { 128, 128, 1, 4, DXGI_FORMAT_D32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
    ID3D11Texture2D *f1a = NULL;
    ID3D11DepthStencilView *f1v = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1a), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)f1a, NULL, &f1v), S_OK);
    const NdView *f1r = rt_com_state_of(f1v, &rt_nulldev_dsv_class);
    T_EQ(f1r->desc.dsv.Format, 40);                     /* DXGI_FORMAT_D32_FLOAT */
    T_EQ(f1r->desc.dsv.ViewDimension, 4);               /* D3D11_DSV_DIMENSION_TEXTURE2DARRAY */
    T_EQ(f1r->desc.dsv.Flags, 0);
    T_EQ(f1r->desc.dsv.Texture2DArray.MipSlice, 0);
    T_EQ(f1r->desc.dsv.Texture2DArray.FirstArraySlice, 0);
    T_EQ(f1r->desc.dsv.Texture2DArray.ArraySize, 4);

    T_CASE("F1: ArraySize 0xffffffff from slice 1 of 4 reads back 3, the read-only flags kept");
    D3D11_DEPTH_STENCIL_VIEW_DESC rest;
    memset(&rest, 0, sizeof rest);
    rest.Format = DXGI_FORMAT_UNKNOWN; rest.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY; rest.Flags = 3;
    rest.Texture2DArray.FirstArraySlice = 1; rest.Texture2DArray.ArraySize = 0xffffffffu;
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)f1a, &rest, &f1v), S_OK);
    f1r = rt_com_state_of(f1v, &rt_nulldev_dsv_class);
    T_EQ(f1r->desc.dsv.Format, 40);
    T_EQ(f1r->desc.dsv.Flags, 3);                       /* READ_ONLY_DEPTH | READ_ONLY_STENCIL, as the game passes them */
    T_EQ(f1r->desc.dsv.Texture2DArray.FirstArraySlice, 1);
    T_EQ(f1r->desc.dsv.Texture2DArray.ArraySize, 3);

    T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilView, device, NULL, &d, &view), "a view on a NULL resource");
    T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)vol, &d, &view), "which is not one of a texture 3D's");
    T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)buf, NULL, &view), "a NULL description on a buffer");
    (void)bad;
}
