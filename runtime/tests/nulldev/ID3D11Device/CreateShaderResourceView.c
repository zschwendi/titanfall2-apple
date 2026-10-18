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

TEST(ID3D11Device, CreateShaderResourceView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *tex = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &tex), S_OK);
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0 };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buf), S_OK);
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture3D *vol = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &vd, NULL, &vol), S_OK);
    LONG dev0 = rt_com_refs(device);
    D3D11_SHADER_RESOURCE_VIEW_DESC d, bad;
    ID3D11ShaderResourceView *view = NULL, *whole = NULL;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MostDetailedMip = 1; d.Texture2D.MipLevels = 2;

    T_CASE("a view with a description: S_OK, the description kept, the device and the resource held");
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, &d, &view), S_OK);
    T_REQUIRE(view != NULL);
    const NdView *v = rt_com_state_of(view, &rt_nulldev_srv_class);
    T_TRUE(!memcmp(&v->desc.srv, &d, sizeof d));
    T_TRUE(v->resource == (ID3D11Resource *)tex);
    T_TRUE(v->child.device == device);
    T_EQ(rt_com_refs(view), 1);
    T_EQ(rt_com_refs(tex), 2);
    T_EQ(rt_com_refs(device), dev0 + 1);

    T_CASE("a NULL description on a texture 2D: its format, TEXTURE2D, every mip level");
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, NULL, &whole), S_OK);
    const NdView *n = rt_com_state_of(whole, &rt_nulldev_srv_class);
    T_EQ(n->desc.srv.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
    T_EQ(n->desc.srv.ViewDimension, 4);                 /* D3D11_SRV_DIMENSION_TEXTURE2D */
    T_EQ(n->desc.srv.Texture2D.MostDetailedMip, 0);
    T_EQ(n->desc.srv.Texture2D.MipLevels, 3);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    LONG before = rt_com_refs(device), tex_before = rt_com_refs(tex);
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), before);
    T_EQ(rt_com_refs(tex), tex_before);

    T_CASE("the view keeps the resource alive after the game releases it");
    T_EQ(rt_com_release(tex), 2);                       /* the two views hold it */
    T_TRUE(rt_com_class_of(tex) == &rt_nulldev_texture2d_class);
    T_EQ(rt_com_release(whole), 0);
    T_EQ(rt_com_refs(tex), 1);

    T_CASE("F1: 256 by 256 with 9 mips and NULL reads back format 28, TEXTURE2D, most detailed 0, 9 levels");
    D3D11_TEXTURE2D_DESC f1d = { 256, 256, 9, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *f1t = NULL, *f1a = NULL;
    ID3D11ShaderResourceView *f1v = NULL;
    D3D11_SHADER_RESOURCE_VIEW_DESC got, rest;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1t), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)f1t, NULL, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11ShaderResourceView, GetDesc, f1v, &got);
    T_EQ(got.Format, 28);
    T_EQ(got.ViewDimension, 4);
    T_EQ(got.Texture2D.MostDetailedMip, 0);
    T_EQ(got.Texture2D.MipLevels, 9);

    T_CASE("F1: format 0 and MipLevels 0xffffffff from mip 2 of 9 read back format 28 and 7 levels");
    memset(&rest, 0, sizeof rest);
    rest.Format = DXGI_FORMAT_UNKNOWN; rest.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    rest.Texture2D.MostDetailedMip = 2; rest.Texture2D.MipLevels = 0xffffffffu;
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)f1t, &rest, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11ShaderResourceView, GetDesc, f1v, &got);
    T_EQ(got.Format, 28);
    T_EQ(got.Texture2D.MostDetailedMip, 2);
    T_EQ(got.Texture2D.MipLevels, 7);

    T_CASE("F1: NULL on a texture with the cube flag: TEXTURECUBE for 6 and 11 slices, a cube array of 2 for 12 and 14 and of 3 for 18");
    static const struct { UINT slices, dim, cubes; } cube[] = { { 6, 9, 0 }, { 11, 9, 0 }, { 12, 10, 2 }, { 14, 10, 2 }, { 18, 10, 3 } };
    for (unsigned i = 0; i < sizeof cube / sizeof cube[0]; i++) {
        D3D11_TEXTURE2D_DESC cd = { 64, 64, 7, cube[i].slices, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                    D3D11_RESOURCE_MISC_TEXTURECUBE };
        ID3D11Texture2D *ct = NULL;
        T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &cd, NULL, &ct), S_OK);
        T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)ct, NULL, &f1v), S_OK);
        memset(&got, 0xa5, sizeof got);
        COM_CALL(ID3D11ShaderResourceView, GetDesc, f1v, &got);
        T_EQ(got.Format, 28);
        T_EQ(got.ViewDimension, cube[i].dim);           /* 9 TEXTURECUBE, 10 TEXTURECUBEARRAY */
        T_EQ(got.TextureCube.MostDetailedMip, 0);
        T_EQ(got.TextureCube.MipLevels, 7);
        if (cube[i].cubes) {
            T_EQ(got.TextureCubeArray.First2DArrayFace, 0);
            T_EQ(got.TextureCubeArray.NumCubes, cube[i].cubes);
        }
    }

    T_CASE("F1: NULL on a 4-slice texture: TEXTURE2DARRAY, every mip level, 4 slices from 0");
    f1d.ArraySize = 4;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &f1d, NULL, &f1a), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)f1a, NULL, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11ShaderResourceView, GetDesc, f1v, &got);
    T_EQ(got.ViewDimension, 5);                         /* D3D11_SRV_DIMENSION_TEXTURE2DARRAY */
    T_EQ(got.Texture2DArray.MostDetailedMip, 0);
    T_EQ(got.Texture2DArray.MipLevels, 9);
    T_EQ(got.Texture2DArray.FirstArraySlice, 0);
    T_EQ(got.Texture2DArray.ArraySize, 4);

    T_CASE("a view of a structured buffer keeps format UNKNOWN and its element range");
    /* the recording's most frequent buffer view (docs/trace/d3d-creators.md: "view UNKNOWN; BUFFER", FirstElement up to 8176) */
    D3D11_BUFFER_DESC sbd = { 208 * 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 208 };
    ID3D11Buffer *sb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &sbd, NULL, &sb), S_OK);
    memset(&rest, 0, sizeof rest);
    rest.Format = DXGI_FORMAT_UNKNOWN; rest.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    rest.Buffer.FirstElement = 16; rest.Buffer.NumElements = 48;
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)sb, &rest, &f1v), S_OK);
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11ShaderResourceView, GetDesc, f1v, &got);
    T_EQ(got.Format, 0);
    T_EQ(got.ViewDimension, 1);
    T_EQ(got.Buffer.FirstElement, 16);
    T_EQ(got.Buffer.NumElements, 48);

    T_TRAPS(COM_CALL(ID3D11Device, CreateShaderResourceView, device, NULL, &d, &view), "a view on a NULL resource");
    T_TRAPS(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)buf, &d, &view), "which is not one of a buffer's");
    T_TRAPS(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)buf, NULL, &view), "a NULL description on a buffer");
    bad = d; bad.Format = 103;                          /* DXGI_FORMAT_NV12 */
    T_TRAPS(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)vol, &bad, &view), "which is not one of a texture 3D's");
    bad.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    T_TRAPS(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)vol, &bad, &view), "which has no size in the format table");
    /* COM_CALL checks that `this` carries the device's vtable first, so the creator's helper is called
     * here with the buffer as its device and a NULL out-pointer: its first statement stops (G8). */
    T_TRAPS(rt_nulldev_view_create(T_CTX(), (ID3D11Device *)buf, &rt_nulldev_srv_class, (ID3D11Resource *)tex, &d, NULL),
            "where an object of class D3D11Device is expected");
}
