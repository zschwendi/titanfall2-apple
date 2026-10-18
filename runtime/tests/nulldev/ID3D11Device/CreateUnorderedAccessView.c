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

TEST(ID3D11Device, CreateUnorderedAccessView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    ID3D11Texture2D *tex = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &tex), S_OK);
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0 };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buf), S_OK);
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture3D *vol = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &vd, NULL, &vol), S_OK);
    LONG dev0 = rt_com_refs(device);
    D3D11_UNORDERED_ACCESS_VIEW_DESC d, bad;
    ID3D11UnorderedAccessView *view = NULL, *whole = NULL;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice = 2;

    T_CASE("a view with a description: S_OK, the description kept, the device and the resource held");
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &d, &view), S_OK);
    T_REQUIRE(view != NULL);
    const NdView *v = rt_com_state_of(view, &rt_nulldev_uav_class);
    T_TRUE(!memcmp(&v->desc.uav, &d, sizeof d));
    T_TRUE(v->resource == (ID3D11Resource *)tex);
    T_TRUE(v->child.device == device);
    T_EQ(rt_com_refs(view), 1);
    T_EQ(rt_com_refs(tex), 2);
    T_EQ(rt_com_refs(device), dev0 + 1);

    T_CASE("a NULL description on a texture 2D: its format, TEXTURE2D, mip level 0");
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, NULL, &whole), S_OK);
    const NdView *n = rt_com_state_of(whole, &rt_nulldev_uav_class);
    T_EQ(n->desc.uav.Format, DXGI_FORMAT_R16G16B16A16_FLOAT);
    T_EQ(n->desc.uav.ViewDimension, 4);                 /* D3D11_UAV_DIMENSION_TEXTURE2D */
    T_EQ(n->desc.uav.Texture2D.MipSlice, 0);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    LONG before = rt_com_refs(device), tex_before = rt_com_refs(tex);
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), before);
    T_EQ(rt_com_refs(tex), tex_before);

    T_CASE("F1: one slice of an 80-slice array as the game makes it is kept as passed, and ArraySize 0xffffffff from slice 77 reads back 3");
    /* docs/trace/d3d-creators.md: "on tex2d R32G32_TYPELESS; view R32G32_FLOAT; TEXTURE2DARRAY", FirstArraySlice 0 to 79, ArraySize 1 */
    D3D11_TEXTURE2D_DESC f1d = { 64, 64, 1, 80, DXGI_FORMAT_R32G32_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *f1a = NULL, *f1b = NULL;
    ID3D11UnorderedAccessView *f1v = NULL;
    D3D11_UNORDERED_ACCESS_VIEW_DESC rest;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1a), S_OK);
    memset(&rest, 0, sizeof rest);
    rest.Format = DXGI_FORMAT_R32G32_FLOAT; rest.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    rest.Texture2DArray.FirstArraySlice = 79; rest.Texture2DArray.ArraySize = 1;
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)f1a, &rest, &f1v), S_OK);
    const NdView *f1r = rt_com_state_of(f1v, &rt_nulldev_uav_class);
    T_TRUE(!memcmp(&f1r->desc.uav, &rest, sizeof rest));
    rest.Texture2DArray.FirstArraySlice = 77; rest.Texture2DArray.ArraySize = 0xffffffffu;
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)f1a, &rest, &f1v), S_OK);
    f1r = rt_com_state_of(f1v, &rt_nulldev_uav_class);
    T_EQ(f1r->desc.uav.Format, 16);                     /* DXGI_FORMAT_R32G32_FLOAT */
    T_EQ(f1r->desc.uav.Texture2DArray.FirstArraySlice, 77);
    T_EQ(f1r->desc.uav.Texture2DArray.ArraySize, 3);

    T_CASE("F1: a 4-slice texture and NULL: TEXTURE2DARRAY, mip level 0, 4 slices from 0");
    f1d.ArraySize = 4; f1d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1b), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)f1b, NULL, &f1v), S_OK);
    f1r = rt_com_state_of(f1v, &rt_nulldev_uav_class);
    T_EQ(f1r->desc.uav.Format, 10);                     /* the texture's: R16G16B16A16_FLOAT */
    T_EQ(f1r->desc.uav.ViewDimension, 5);               /* D3D11_UAV_DIMENSION_TEXTURE2DARRAY */
    T_EQ(f1r->desc.uav.Texture2DArray.MipSlice, 0);
    T_EQ(f1r->desc.uav.Texture2DArray.FirstArraySlice, 0);
    T_EQ(f1r->desc.uav.Texture2DArray.ArraySize, 4);

    T_TRAPS(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, NULL, &d, &view), "a view on a NULL resource");
    bad = d; bad.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    T_TRAPS(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &bad, &view), "which is not one of a texture 2D's");
    T_TRAPS(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)vol, NULL, &view), "a NULL description on a texture 3D");
    (void)buf;
}
