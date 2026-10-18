/* Expected values, worked by hand from Microsoft's pages (docs/handoff/d3d-resources.md,
 * section 2, has the arithmetic):
 *   BC1 is 8 bytes per 4 by 4 block ("Texture Block Compression in Direct3D 11"); a row of
 *   blocks is max(1, (width + 3) / 4) * 8 bytes (the DDS programming guide); each level halves,
 *   down to 1 (CreateTexture2D's example).  Levels 256, 128, 64, 32, 16, 8, 4, 2, 1 have 64,
 *   32, 16, 8, 4, 2, 1, 1, 1 blocks a side: 32768 + 8192 + 2048 + 512 + 128 + 32 + 8 + 8 + 8 =
 *   43704 bytes (the levels of 2 by 2 and 1 by 1 are one whole block each).
 *   R8G8B8A8_UNORM is a 32-bit format (DXGI_FORMAT): 4 by 4 is 64 bytes, 2 by 2 is 16, 1 by 1
 *   is 4; 84 a slice, 504 for 6 slices; subresource index = mip + slice * levels
 *   (D3D11CalcSubresource).
 *   The 2 by 2 texture of the copy case has rows of 8 bytes; its initial data has rows 12
 *   bytes apart (D3D11_SUBRESOURCE_DATA: SysMemPitch may "include padding"). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateTexture2D)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC d = { 256, 256, 0, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 }, bad;
    ID3D11Texture2D *tex = NULL;
    static const uint64_t bc1_bytes[9] = { 32768, 8192, 2048, 512, 128, 32, 8, 8, 8 };
    NdLevel l;

    T_CASE("256 by 256 of BC1_UNORM with MipLevels 0: 9 levels in the description, 9 subresources, 43704 bytes");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &d, NULL, &tex), S_OK);
    T_REQUIRE(tex != NULL);
    NdTexture *t = rt_com_state_of(tex, &rt_nulldev_texture2d_class);
    T_EQ(t->dim, D3D11_RESOURCE_DIMENSION_TEXTURE2D);
    T_EQ(t->desc.d2.MipLevels, 9);
    T_EQ(t->desc.d2.Format, DXGI_FORMAT_BC1_UNORM);
    T_EQ(t->mem.nsub, 9);
    T_EQ(t->mem.size, 43704);
    T_EQ(rt_com_refs(tex), 1);
    T_EQ(rt_com_refs(device), 3);
    for (UINT i = 0; i < 9; i++) {
        T_TRUE(rt_nulldev_sub(tex, i, &l));
        T_EQ(l.bytes, bc1_bytes[i]);
    }
    T_TRUE(!rt_nulldev_sub(tex, 9, &l));

    T_CASE("6 slices of 4 by 4 R8G8B8A8_UNORM with 3 levels: 18 subresources, 504 bytes");
    D3D11_TEXTURE2D_DESC cube = { 4, 4, 3, 6, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                  D3D11_RESOURCE_MISC_TEXTURECUBE };
    ID3D11Texture2D *six = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &cube, NULL, &six), S_OK);
    NdTexture *t6 = rt_com_state_of(six, &rt_nulldev_texture2d_class);
    T_EQ(t6->mem.nsub, 18);
    T_EQ(t6->mem.size, 504);
    T_TRUE(rt_nulldev_sub(six, 3 * 4 + 1, &l));         /* level 1 of slice 4 */
    T_EQ(l.width, 2);
    T_EQ(l.row_bytes, 8);
    T_EQ(l.bytes, 16);
    T_TRUE(rt_nulldev_sub(six, 17, &l));
    T_EQ(l.bytes, 4);

    T_CASE("initial data is copied row by row from a pitch larger than the row");
    D3D11_TEXTURE2D_DESC small = { 2, 2, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
    uint8_t src[20];
    static const uint8_t want[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 16, 17, 18, 19, 20 };
    for (int i = 0; i < 20; i++) src[i] = (uint8_t)(i + 1);
    D3D11_SUBRESOURCE_DATA init = { src, 12, 0 };
    ID3D11Texture2D *filled = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, &init, &filled), S_OK);
    T_TRUE(!memcmp(rt_nulldev_block(filled, 0)->data, want, 16));
    /* unit d3d-fix-2, item 5: the same data for a texture the game cannot map is read and dropped, and no block is made */
    D3D11_TEXTURE2D_DESC fixed = small;
    fixed.Usage = D3D11_USAGE_IMMUTABLE; fixed.BindFlags = D3D11_BIND_SHADER_RESOURCE; fixed.CPUAccessFlags = 0;
    ID3D11Texture2D *unkept = NULL;
    NdTotals tot0, tot1;
    rt_nulldev_totals(&tot0);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &fixed, &init, &unkept), S_OK);
    rt_nulldev_totals(&tot1);
    T_TRUE(rt_nulldev_memory(unkept)->blocks == NULL);
    T_EQ(rt_nulldev_content(unkept, 0), ND_CONTENT_DROPPED);
    T_EQ(tot1.resources, tot0.resources + 1);
    T_EQ(tot1.bytes, tot0.bytes);
    T_EQ(tot1.dropped_bytes, tot0.dropped_bytes + 16);
    T_EQ(rt_com_release(unkept), 0);
    rt_nulldev_totals(&tot1);
    T_EQ(tot1.resources, tot0.resources);

    T_CASE("a multisampled texture has no memory");
    D3D11_TEXTURE2D_DESC ms = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture2D *msaa = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &msaa), S_OK);
    NdTexture *tm = rt_com_state_of(msaa, &rt_nulldev_texture2d_class);
    T_EQ(tm->mem.size, 0);
    T_EQ(tm->mem.nsub, 1);
    T_TRUE(tm->mem.blocks == NULL);
    T_EQ(tm->desc.d2.SampleDesc.Count, 4);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object left");
    LONG before = rt_com_refs(device);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &d, NULL, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), before);

    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, NULL, NULL, &tex), "CreateTexture2D with a NULL description");
    bad = d; bad.Format = 103;                          /* DXGI_FORMAT_NV12 */
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &bad, NULL, &tex), "a format that has no size in the format table");
    bad = d; bad.MipLevels = 10;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &bad, NULL, &tex), "more mip levels than the full chain");
    bad = d; bad.Height = 0;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &bad, NULL, &tex), "a texture 2D description with a size of 0");
    bad = d; bad.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &bad, NULL, &tex), "CPU read access without staging usage");
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, &init, &tex), "initial data for a multisampled texture");
    init.SysMemPitch = 7;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, &init, &tex), "smaller than a row of 8 bytes");
    /* G5 of docs/reviews/d3d-code-review.md: feature level 11_0's limits (format-table review, C14) */
    D3D11_TEXTURE2D_DESC many = { 4, 4, 1, 2049, DXGI_FORMAT_R8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &many, NULL, &tex), "an array size of 2049, above the limit of 2048");
    D3D11_TEXTURE2D_DESC tall = { 4, 16385, 1, 1, DXGI_FORMAT_R8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &tall, NULL, &tex), "a height of 16385, above the limit of 16384");
    D3D11_TEXTURE2D_DESC most = { 16384, 16384, 1, 2048, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &most, NULL, NULL), S_FALSE);
    D3D11_TEXTURE2D_DESC huge = { 16384, 16384, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *big = NULL;
    D3D11_MAPPED_SUBRESOURCE bm;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &huge, NULL, &big), S_OK);
    T_EQ(rt_nulldev_memory(big)->size, UINT64_C(1) << 32);     /* 16384 * 16384 * 16 */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)big, 0, D3D11_MAP_WRITE, 0, &bm), "does not fit DepthPitch's 32 bits");
    /* unit d3d-fix-2: a sample count the device's CheckMultisampleQualityLevels refuses (format-table review C11; red team R9) */
    D3D11_TEXTURE2D_DESC s8 = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 8, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &s8, NULL, &tex), "a sample count or quality the device's own CheckMultisampleQualityLevels refuses");
    s8.SampleDesc.Count = 4; s8.SampleDesc.Quality = 1;
    T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &s8, NULL, &tex), "a sample count or quality the device's own CheckMultisampleQualityLevels refuses");
    s8.SampleDesc.Quality = 0;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &s8, NULL, NULL), S_FALSE);
}
