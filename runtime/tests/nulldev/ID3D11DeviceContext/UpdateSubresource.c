/* Expected values: Microsoft's page of the method (the box in texels, or bytes for a buffer;
 * SrcRowPitch "the size of one row of the source data"; NULL is the whole subresource; an empty
 * box is a no-op; immutable and dynamic resources cannot be destinations).  The byte patterns
 * are the test's own.  The constant-buffer case is the recording's second most frequent update
 * (docs/trace/d3d-map.md: "buffer DEFAULT bind=CONSTANT_BUFFER; sub 0; box=NULL", 576 bytes). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

#define UPDATE(dst, sub, box, data, row, depth) \
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)(dst), (sub), (box), (data), (row), (depth))

TEST(ID3D11DeviceContext, UpdateSubresource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_MAPPED_SUBRESOURCE m;
    uint8_t src[4 * 24];
    for (unsigned i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i + 1);

    T_CASE("the bytes of a box of a staging texture are read back at the next read mapping, rows SrcRowPitch apart");
    D3D11_TEXTURE2D_DESC sd = { 8, 8, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *tex = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &tex), S_OK);
    uint8_t dd[8 * 8 * 4];
    memset(dd, 0xdd, sizeof dd);
    UPDATE(tex, 0, NULL, dd, 32, 0);                    /* all of it, by a call the device sees */
    D3D11_BOX box = { 2, 3, 0, 6, 7, 1 };               /* 4 by 4 texels at (2, 3): rows of 16 bytes, 24 apart in the source */
    UPDATE(tex, 0, &box, src, 24, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)tex, 0, D3D11_MAP_READ, 0, &m), S_OK);
    const uint8_t *p = m.pData;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++) {
            bool in = y >= 3 && y < 7 && x >= 8 && x < 24;
            T_EQ(p[y * 32 + x], in ? src[(y - 3) * 24 + (x - 8)] : 0xdd);
        }
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)tex, 0);

    T_CASE("a buffer takes the bytes of its box as they lie");
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buf), S_OK);
    D3D11_BOX bytes = { 16, 0, 0, 48, 1, 1 };
    UPDATE(buf, 0, &bytes, src, 0, 0);                  /* the pitches mean nothing for a buffer */
    T_EQ(rt_nulldev_block(buf, 0)->data[16], 1);
    T_EQ(rt_nulldev_block(buf, 0)->data[47], 32);

    T_CASE("a box written into a subresource nothing was put in leaves the rest unwritten: a read mapping is a stop until updates have covered every byte");
    /* docs/reviews/night-2026-10-11-code-review.md, finding 3: bytes 0 to 15 and 48 to 63 were
     * never written; Windows calls them undefined (the device's rule: undefined is a stop). */
    T_EQ(rt_nulldev_content(buf, 0), ND_CONTENT_PART);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_READ, 0, &m),
            "a read mapping of a subresource of which calls wrote only a part (subresource 0: 32 of 64");
    D3D11_BOX head = { 0, 0, 0, 20, 1, 1 }, tail = { 48, 0, 0, 64, 1, 1 };
    UPDATE(buf, 0, &head, src + 40, 0, 0);              /* 0 to 20: overlaps what was written */
    T_EQ(rt_nulldev_content(buf, 0), ND_CONTENT_PART);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_READ, 0, &m), "(subresource 0: 48 of 64");
    UPDATE(buf, 0, &tail, src + 70, 0, 0);
    T_EQ(rt_nulldev_content(buf, 0), ND_CONTENT_GAME);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_READ, 0, &m), S_OK);
    p = m.pData;
    for (int i = 0; i < 64; i++) T_EQ(p[i], i < 20 ? src[40 + i] : i < 48 ? src[i - 16] : src[70 + i - 48]);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);

    T_CASE("boxes of a texture add up row by row, and a level is the game's only when every texel is");
    ID3D11Texture2D *quad = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &quad), S_OK);
    D3D11_BOX left = { 0, 0, 0, 4, 8, 1 }, top_right = { 4, 0, 0, 8, 4, 1 }, bottom_right = { 4, 4, 0, 8, 8, 1 };
    UPDATE(quad, 0, &left, dd, 16, 0);
    UPDATE(quad, 0, &top_right, dd, 16, 0);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)quad, 0, D3D11_MAP_READ, 0, &m), "(subresource 0: 48 of 64");
    UPDATE(quad, 0, &bottom_right, src, 16, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)quad, 0, D3D11_MAP_READ, 0, &m), S_OK);
    p = m.pData;
    T_EQ(p[4 * 32 + 15], 0xdd); T_EQ(p[4 * 32 + 16], 1); T_EQ(p[7 * 32 + 31], 64);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)quad, 0);

    T_CASE("the recording's update of a whole constant buffer of 576 bytes is taken and the buffer holds the game's bytes");
    static uint8_t constants[576];
    D3D11_BUFFER_DESC cd = { 576, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    ID3D11Buffer *cb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &cd, NULL, &cb), S_OK);
    T_EQ(rt_nulldev_content(cb, 0), ND_CONTENT_NONE);
    UPDATE(cb, 0, NULL, constants, 576, 576);
    T_TRUE(rt_nulldev_content(cb, 0) == ND_CONTENT_GAME || rt_nulldev_content(cb, 0) == ND_CONTENT_DROPPED);

    T_CASE("an empty box does nothing");
    D3D11_BOX none = { 4, 0, 0, 4, 1, 1 };
    ID3D11Buffer *fresh = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &fresh), S_OK);
    UPDATE(fresh, 0, &none, src, 0, 0);
    T_EQ(rt_nulldev_content(fresh, 0), ND_CONTENT_NONE);

    D3D11_BUFFER_DESC dyn = { 64, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *vb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &dyn, NULL, &vb), S_OK);
    T_TRAPS(UPDATE(vb, 0, NULL, src, 0, 0), "UpdateSubresource of a resource created with immutable or dynamic usage");
    T_TRAPS(UPDATE(buf, 0, NULL, NULL, 0, 0), "UpdateSubresource with NULL data");
    D3D11_BOX far = { 60, 0, 0, 68, 1, 1 };
    T_TRAPS(UPDATE(buf, 0, &far, src, 0, 0), "outside the destination");
    T_TRAPS(UPDATE(tex, 0, &box, src, 8, 0), "UpdateSubresource with SrcRowPitch 8, smaller than a row of 16 bytes");
    T_TRAPS(UPDATE(NULL, 0, NULL, src, 0, 0), "a copy or an update with a NULL destination");
}
