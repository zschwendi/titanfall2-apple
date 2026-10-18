/* Expected values: the description is the one the test itself passes to the creator, as the
 * game makes its views of a typeless render target (docs/trace/d3d-creators.md,
 * CreateShaderResourceView: "on tex2d R16G16B16A16_TYPELESS; view R16G16B16A16_FLOAT; TEXTURE2D",
 * MostDetailedMip 0, MipLevels 1); format numbers 9, 10 and 28 and the dimension 4 are the
 * enumerations of rt/win/dxgi.h and rt/win/d3d11view.h (MinGW's headers); the size 24 is
 * D3D11_SHADER_RESOURCE_VIEW_DESC's Windows layout (the same header's assertion).  For a NULL
 * description: Microsoft's page of CreateShaderResourceView ("a view that accesses the entire
 * resource (using the format the resource was created with)") and Wine's
 * test_create_shader_resource_view as docs/reviews/d3d-contract-check-2.md, F1, reads it (the
 * texture's format, TEXTURE2D, MostDetailedMip 0, every mip level: 9 for 256 by 256). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11ShaderResourceView, GetDesc)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 1920, 1080, 1, 1, DXGI_FORMAT_R16G16B16A16_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_TEXTURE2D_DESC pd = { 256, 256, 9, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *tex = NULL, *plain = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &tex), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &pd, NULL, &plain), S_OK);
    ID3D11ShaderResourceView *view = NULL, *whole = NULL;
    uint8_t raw[24 + 8], want[24];

    T_CASE("the description the view was made with, all 24 bytes and nothing after them");
    D3D11_SHADER_RESOURCE_VIEW_DESC d;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    d.Texture2D.MostDetailedMip = 0; d.Texture2D.MipLevels = 1;
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, &d, &view), S_OK);
    T_REQUIRE(view != NULL);
    memset(want, 0, sizeof want);
    memcpy(want, &(uint32_t){ 10 }, 4);                 /* Format: R16G16B16A16_FLOAT */
    memcpy(want + 4, &(uint32_t){ 4 }, 4);              /* ViewDimension: TEXTURE2D */
    memcpy(want + 12, &(uint32_t){ 1 }, 4);             /* Texture2D.MipLevels (MostDetailedMip at 8 is 0) */
    memset(raw, 0xa5, sizeof raw);
    COM_CALL(ID3D11ShaderResourceView, GetDesc, view, (D3D11_SHADER_RESOURCE_VIEW_DESC *)raw);
    T_TRUE(!memcmp(raw, want, 24));
    for (int i = 24; i < 32; i++) T_EQ(raw[i], 0xa5);
    T_EQ(rt_com_refs(view), 1);

    T_CASE("a view made with a NULL description: the one the creator derived");
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)plain, NULL, &whole), S_OK);
    T_REQUIRE(whole != NULL);
    D3D11_SHADER_RESOURCE_VIEW_DESC got;
    memset(&got, 0xa5, sizeof got);
    COM_CALL(ID3D11ShaderResourceView, GetDesc, whole, &got);
    T_EQ(got.Format, 28);                               /* the texture's: R8G8B8A8_UNORM */
    T_EQ(got.ViewDimension, 4);
    T_EQ(got.Texture2D.MostDetailedMip, 0);
    T_EQ(got.Texture2D.MipLevels, 9);

    T_TRAPS(COM_CALL(ID3D11ShaderResourceView, GetDesc, view, NULL), "ID3D11ShaderResourceView::GetDesc with a NULL description");
}
