/* Expected values: a copy between two resources whose bytes the game wrote is a copy of those
 * bytes, by Microsoft's page of the method (box in bytes for buffers, in texels for textures;
 * an empty box is a no-op; NULL is the whole subresource); the byte patterns are the test's
 * own, and where a byte should lie follows from the layout rules of memory.c's sources (a
 * BC1 block is 8 bytes, a row of blocks ceil(width / 4) of them: Microsoft's block-compression
 * page and the DDS guide).  That a copy from a render target leaves contents only a renderer
 * could produce, and that reading them is a stop, is the device's rule (rt/nulldev.h,
 * ND_CONTENT_*; docs/handoff/d3d-fix-2.md, item 3).  The streaming copy is the recording's most
 * frequent one (docs/trace/d3d-map.md: BC1 texture to BC1 texture, box NULL, at the origin,
 * destination subresource one above the source's). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

static uint8_t *map(ID3D11DeviceContext *context, void *res, UINT sub, UINT type) {
    D3D11_MAPPED_SUBRESOURCE m = { 0 };
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)res, sub, type, 0, &m), S_OK);
    return m.pData;
}
#define UNMAP(res, sub) COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)(res), (sub))
#define UPDATE(dst, data, pitch) \
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)(dst), 0, NULL, (data), (pitch), 0)
#define COPY(dst, dsub, x, y, z, src, ssub, box) \
    COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)(dst), (dsub), (x), (y), (z), (ID3D11Resource *)(src), (ssub), (box))

TEST(ID3D11DeviceContext, CopySubresourceRegion)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC stg = { 64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *a = NULL, *b = NULL, *empty = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &a), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &b), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &empty), S_OK);
    /* Filled by UpdateSubresource, a write the device sees: what the game writes through the
     * pointer of a writing Map it cannot see, and does not count as written (Map.c). */
    uint8_t fill[32 * 32 * 4], *p;
    for (int i = 0; i < 64; i++) fill[i] = (uint8_t)(0x40 + i);
    UPDATE(a, fill, 0);
    memset(fill, 0xee, 64);
    UPDATE(b, fill, 0);

    T_CASE("a box of a staging buffer the game wrote is copied to its offset in another, and the rest stays");
    D3D11_BOX box = { 8, 0, 0, 24, 1, 1 };              /* bytes 8 to 24 */
    COPY(b, 0, 40, 0, 0, a, 0, &box);
    p = map(context, b, 0, D3D11_MAP_READ);
    for (int i = 0; i < 64; i++) T_EQ(p[i], i >= 40 && i < 56 ? (uint8_t)(0x40 + 8 + i - 40) : 0xee);
    UNMAP(b, 0);
    T_EQ(rt_nulldev_content(b, 0), ND_CONTENT_GAME);

    T_CASE("a block of a block-compressed staging texture is copied to another block");
    D3D11_TEXTURE2D_DESC bc = { 8, 8, 1, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *ta = NULL, *tb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &bc, NULL, &ta), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &bc, NULL, &tb), S_OK);
    for (int i = 0; i < 32; i++) fill[i] = (uint8_t)(i + 1);
    UPDATE(ta, fill, 16);                               /* 2 by 2 blocks of 8 bytes, row pitch 16 */
    memset(fill, 0x77, 32);
    UPDATE(tb, fill, 16);
    D3D11_BOX blk = { 4, 0, 0, 8, 4, 1 };               /* block (1, 0): bytes 8 to 16 of row 0 */
    COPY(tb, 0, 0, 4, 0, ta, 0, &blk);                  /* to block (0, 1): bytes 0 to 8 of row 1 */
    p = map(context, tb, 0, D3D11_MAP_READ);
    for (int i = 0; i < 32; i++) T_EQ(p[i], i >= 16 && i < 24 ? (uint8_t)(8 + i - 16 + 1) : 0x77);
    UNMAP(tb, 0);

    T_CASE("an empty box does nothing");
    D3D11_BOX none = { 8, 0, 0, 8, 1, 1 };
    COPY(b, 0, 0, 0, 0, a, 0, &none);
    p = map(context, b, 0, D3D11_MAP_READ);
    T_EQ(p[0], 0xee);
    UNMAP(b, 0);

    T_CASE("a copy from a subresource nothing was put in gives zeroes and no contents to read");
    D3D11_BOX part = { 0, 0, 0, 4, 1, 1 };
    COPY(b, 0, 0, 0, 0, empty, 0, &part);               /* a part: 4 bytes of b are no longer the game's */
    T_EQ(rt_nulldev_content(b, 0), ND_CONTENT_PART);
    T_EQ(rt_nulldev_block(b, 0)->data[3], 0); T_EQ(rt_nulldev_block(b, 0)->data[4], 0xee);
    /* docs/reviews/night-2026-10-11-code-review.md, finding 3: the four bytes are what Windows
     * calls undefined, so reading the subresource is the stop it is for one never written */
    T_TRAPS(map(context, b, 0, D3D11_MAP_READ), "a read mapping of a subresource of which calls wrote only a part (subresource 0: 60 of 64");

    T_CASE("a box copied out of the written part of such a subresource is the game's, and a box that touches the unwritten part is not");
    ID3D11Buffer *c = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &c), S_OK);
    memset(fill, 0x55, 64);
    UPDATE(c, fill, 0);
    D3D11_BOX good = { 4, 0, 0, 12, 1, 1 }, bad = { 2, 0, 0, 6, 1, 1 };
    COPY(c, 0, 0, 0, 0, b, 0, &good);
    T_EQ(rt_nulldev_content(c, 0), ND_CONTENT_GAME);
    p = map(context, c, 0, D3D11_MAP_READ);
    T_EQ(p[0], 0xee); T_EQ(p[7], 0xee); T_EQ(p[8], 0x55);
    UNMAP(c, 0);
    COPY(c, 0, 16, 0, 0, b, 0, &bad);                   /* b's bytes 2 and 3 are unwritten, 4 and 5 the game's */
    T_EQ(rt_nulldev_content(c, 0), ND_CONTENT_PART);
    T_TRAPS(map(context, c, 0, D3D11_MAP_READ), "(subresource 0: 62 of 64");
    D3D11_BOX mend = { 16, 0, 0, 18, 1, 1 };
    COPY(c, 0, 16, 0, 0, a, 0, &mend);                  /* the two bytes again, from a buffer that is all the game's */
    T_EQ(rt_nulldev_content(c, 0), ND_CONTENT_GAME);
    p = map(context, c, 0, D3D11_MAP_READ);
    T_EQ(p[16], 0x50); T_EQ(p[17], 0x51); T_EQ(p[18], 0xee); T_EQ(p[19], 0xee); T_EQ(p[20], 0x55);
    UNMAP(c, 0);

    T_CASE("a copy of all of a subresource nothing was put in leaves nothing to read");
    COPY(b, 0, 0, 0, 0, empty, 0, NULL);                /* all of it */
    T_EQ(rt_nulldev_content(b, 0), ND_CONTENT_NONE);
    T_TRAPS(map(context, b, 0, D3D11_MAP_READ), "a read mapping of a subresource no call wrote");
    memset(fill, 0xee, 64);
    UPDATE(b, fill, 0);                                 /* the game's again, for the stops below */

    T_CASE("a copy from a render target leaves the destination a renderer's, also when it covers a part");
    D3D11_TEXTURE2D_DESC rt = { 32, 32, 1, 1, DXGI_FORMAT_R32_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_TEXTURE2D_DESC rb = { 32, 32, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *target = NULL, *back = NULL;
    D3D11_MAPPED_SUBRESOURCE m;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &rt, NULL, &target), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &rb, NULL, &back), S_OK);
    memset(fill, 0x11, sizeof fill);
    UPDATE(back, fill, 32 * 4);
    D3D11_BOX corner = { 0, 0, 0, 4, 4, 1 };
    COPY(back, 0, 8, 8, 0, target, 0, &corner);
    T_EQ(rt_nulldev_content(back, 0), ND_CONTENT_RENDERER);
    T_EQ(rt_nulldev_block(back, 0)->data[0], 0x11);     /* no byte was invented */

    T_CASE("the recording's copy of mip k to mip k + 1 of another texture is taken");
    static uint8_t texels[8 * 8 / 16 * 8 + 8 + 8];      /* BC1 8 by 8, 4 by 4, 2 by 2 (one block each below 4): 32 + 8 + 8 */
    D3D11_SUBRESOURCE_DATA init3[3] = { { texels, 16, 0 }, { texels + 32, 8, 0 }, { texels + 40, 8, 0 } };
    D3D11_SUBRESOURCE_DATA init4[4] = { { texels, 32, 0 }, { texels, 16, 0 }, { texels + 32, 8, 0 }, { texels + 40, 8, 0 } };
    static uint8_t top[16 * 16 / 16 * 8];
    init4[0].pSysMem = top;
    D3D11_TEXTURE2D_DESC small = { 8, 8, 3, 1, DXGI_FORMAT_BC1_UNORM_SRGB, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_TEXTURE2D_DESC large = { 16, 16, 4, 1, DXGI_FORMAT_BC1_UNORM_SRGB, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *from = NULL, *to = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, init3, &from), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &large, init4, &to), S_OK);
    for (UINT k = 0; k < 3; k++) COPY(to, k + 1, 0, 0, 0, from, k, NULL);
    T_TRUE(rt_nulldev_content(to, 3) == ND_CONTENT_GAME || rt_nulldev_content(to, 3) == ND_CONTENT_DROPPED);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)back, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource that a copy from a renderer's output or a resolve wrote");
    T_TRAPS(COPY(NULL, 0, 0, 0, 0, a, 0, NULL), "a copy or an update with a NULL destination");
    T_TRAPS(COPY(b, 1, 0, 0, 0, a, 0, NULL), "a copy or an update of a subresource the destination does not have");
    D3D11_BOX far = { 60, 0, 0, 68, 1, 1 };
    T_TRAPS(COPY(b, 0, 0, 0, 0, a, 0, &far), "outside the source");
    T_TRAPS(COPY(b, 0, 56, 0, 0, a, 0, &box), "which Microsoft calls undefined");
    D3D11_BOX off = { 2, 0, 0, 8, 4, 1 };
    T_TRAPS(COPY(tb, 0, 0, 0, 0, ta, 0, &off), "does not lie on the grid");
    T_TRAPS(COPY(back, 0, 0, 0, 0, ta, 0, NULL), "a copy between formats whose elements differ in size");
    T_TRAPS(COPY(b, 0, 0, 0, 0, ta, 0, NULL), "a copy between resources of different type");
    T_TRAPS(COPY(a, 0, 0, 0, 0, a, 0, NULL), "onto itself");
    T_TRAPS(COPY(from, 0, 0, 0, 0, to, 1, NULL), "a copy into an immutable resource");
}
