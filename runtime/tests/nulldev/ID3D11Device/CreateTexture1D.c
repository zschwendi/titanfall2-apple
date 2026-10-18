/* Expected values, worked by hand from Microsoft's pages: CreateTexture1D ("For a 32 width
 * texture with a full mipmap chain, the pInitialData array has the following 6 elements": 32,
 * 16, 8, 4, 2, 1) and DXGI_FORMAT (R8_UNORM is an 8-bit format): 32 + 16 + 8 + 4 + 2 + 1 = 63
 * bytes.  A NULL ppTexture1D validates and returns S_FALSE (the same page). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateTexture1D)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE1D_DESC d = { 32, 0, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 }, bad;
    ID3D11Texture1D *tex = NULL;
    static const uint32_t width[6] = { 32, 16, 8, 4, 2, 1 };
    uint8_t bytes[6][32];
    D3D11_SUBRESOURCE_DATA init[6];

    T_CASE("width 32 of R8_UNORM with MipLevels 0: 6 levels in the description, 6 subresources, 63 bytes");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &d, NULL, &tex), S_OK);
    T_REQUIRE(tex != NULL);
    NdTexture *t = rt_com_state_of(tex, &rt_nulldev_texture1d_class);
    T_EQ(t->dim, D3D11_RESOURCE_DIMENSION_TEXTURE1D);
    T_EQ(t->desc.d1.MipLevels, 6);
    T_EQ(t->desc.d1.Width, 32);
    T_EQ(t->mem.nsub, 6);
    T_EQ(t->mem.size, 63);
    T_EQ(rt_com_refs(tex), 1);
    T_EQ(rt_com_refs(device), 3);
    for (UINT i = 0; i < 6; i++) {
        NdLevel l;
        T_TRUE(rt_nulldev_sub(tex, i, &l));
        T_EQ(l.width, width[i]);
        T_EQ(l.bytes, width[i]);
        T_EQ(l.row_bytes, width[i]);
    }

    T_CASE("initial data is copied as it lies");
    for (int i = 0; i < 6; i++) {
        for (int x = 0; x < 32; x++) bytes[i][x] = (uint8_t)(16 * i + x + 1);
        init[i] = (D3D11_SUBRESOURCE_DATA){ bytes[i], 0, 0 };
    }
    ID3D11Texture1D *filled = NULL;
    D3D11_TEXTURE1D_DESC kept = d;                      /* bytes are kept only for a texture the game can map (unit d3d-fix-2, item 5) */
    kept.Usage = D3D11_USAGE_STAGING; kept.BindFlags = 0; kept.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture1D *unkept = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &d, init, &unkept), S_OK);
    T_TRUE(rt_nulldev_memory(unkept)->blocks == NULL);  /* the DEFAULT one: its data was read and dropped */
    T_EQ(rt_nulldev_content(unkept, 5), ND_CONTENT_DROPPED);
    T_EQ(rt_com_release(unkept), 0);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &kept, init, &filled), S_OK);
    T_EQ(rt_nulldev_content(filled, 5), ND_CONTENT_GAME);
    for (UINT i = 0; i < 6; i++)
        T_TRUE(!memcmp(rt_nulldev_block(filled, i)->data, bytes[i], width[i]));

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object left");
    T_EQ(rt_com_refs(device), 4);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &d, NULL, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 4);

    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture1D, device, NULL, NULL, &tex), "CreateTexture1D with a NULL description");
    bad = d; bad.Width = 0;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture1D, device, &bad, NULL, &tex), "a texture 1D description with a size of 0");
    /* G5 of docs/reviews/d3d-code-review.md: feature level 11_0's limits (format-table review, C14) */
    D3D11_TEXTURE1D_DESC wide = { 16385, 1, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture1D, device, &wide, NULL, &tex), "a width of 16385, above the limit of 16384");
}
