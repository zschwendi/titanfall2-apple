/* Expected values, worked by hand from Microsoft's pages: CreateTexture3D ("for a 32 x 32 x 4
 * volume texture with a full mipmap chain, the array has the following 6 elements": 32 x 32
 * with 4 slices, 16 x 16 with 2, 8 x 8, 4 x 4, 2 x 2 and 1 x 1 with 1 slice each) and DXGI_FORMAT
 * (R8_UNORM is an 8-bit format): 4096 + 512 + 64 + 16 + 4 + 1 = 4693 bytes; all slices of a
 * level are one subresource (D3D11CalcSubresource).  The copy case: 2 by 2 by 2, rows 3 bytes
 * apart and slices 8 bytes apart in the game's data (D3D11_SUBRESOURCE_DATA). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateTexture3D)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE3D_DESC d = { 32, 32, 4, 0, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 }, bad;
    ID3D11Texture3D *tex = NULL;
    static const uint64_t bytes[6] = { 4096, 512, 64, 16, 4, 1 };
    static const uint32_t depth[6] = { 4, 2, 1, 1, 1, 1 };

    T_CASE("32 by 32 by 4 of R8_UNORM with MipLevels 0: 6 levels in the description, 6 subresources, 4693 bytes");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &d, NULL, &tex), S_OK);
    T_REQUIRE(tex != NULL);
    NdTexture *t = rt_com_state_of(tex, &rt_nulldev_texture3d_class);
    T_EQ(t->dim, D3D11_RESOURCE_DIMENSION_TEXTURE3D);
    T_EQ(t->desc.d3.MipLevels, 6);
    T_EQ(t->mem.nsub, 6);
    T_EQ(t->mem.size, 4693);
    T_EQ(rt_com_refs(device), 3);
    for (UINT i = 0; i < 6; i++) {
        NdLevel l;
        T_TRUE(rt_nulldev_sub(tex, i, &l));
        T_EQ(l.bytes, bytes[i]);
        T_EQ(l.depth, depth[i]);
        T_EQ(l.slice_bytes, bytes[i] / depth[i]);
    }

    T_CASE("initial data is copied with its rows and slices apart");
    D3D11_TEXTURE3D_DESC small = { 2, 2, 2, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };   /* mappable: its bytes are kept */
    uint8_t src[13];
    static const uint8_t want[8] = { 1, 2, 4, 5, 9, 10, 12, 13 };
    for (int i = 0; i < 13; i++) src[i] = (uint8_t)(i + 1);
    D3D11_SUBRESOURCE_DATA init = { src, 3, 8 };
    ID3D11Texture3D *filled = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &small, &init, &filled), S_OK);
    T_TRUE(!memcmp(rt_nulldev_block(filled, 0)->data, want, 8));

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object left");
    LONG before = rt_com_refs(device);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &d, NULL, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), before);

    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture3D, device, NULL, NULL, &tex), "CreateTexture3D with a NULL description");
    bad = d; bad.Depth = 0;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture3D, device, &bad, NULL, &tex), "a texture 3D description with a size of 0");
    init.SysMemSlicePitch = 4;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture3D, device, &small, &init, &tex), "smaller than a slice of 2 rows");
    /* G5 of docs/reviews/d3d-code-review.md: feature level 11_0's limits (format-table review, C14) */
    D3D11_TEXTURE3D_DESC wide = { 4096, 4, 4, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture3D, device, &wide, NULL, &tex), "a width of 4096, above the limit of 2048");
    D3D11_TEXTURE3D_DESC most = { 2048, 2048, 2048, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &most, NULL, NULL), S_FALSE);
    most.Depth = 2049;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture3D, device, &most, NULL, &tex), "a depth of 2049, above the limit of 2048");
}
