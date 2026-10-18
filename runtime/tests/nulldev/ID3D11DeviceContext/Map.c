/* Expected values.  Pitches and sizes, by hand from Microsoft's pages (docs/handoff/
 * d3d-resources.md, section 2) and from Wine's d3d11 test test_resource_map as the contract's
 * check quotes it (docs/reviews/d3d-contract-check.md, E6): a buffer of 1024 bytes has both
 * pitches 1024; a 512 by 512 R8G8B8A8 texture 2048 and 1048576; a 64 cubed R8G8B8A8 texture
 * 256 and 16384; pData is a multiple of 16.  BC1 10 by 6: 3 by 2 blocks of 8 bytes, 24 and 48.
 * Level 1 of an 8 by 8 R8G8B8A8 texture is 4 by 4: 16 and 64.  Which combinations Windows
 * refuses: the same check, from Wine's check_resource_cpu_access_ (each is a stop here).  What
 * a mapping finds in the memory is this device's own rule (Map.c): only what the test wrote. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, Map)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_MAPPED_SUBRESOURCE m;
    uint8_t bytes[1024];
    for (int i = 0; i < 1024; i++) bytes[i] = (uint8_t)(i * 3 + 5);
    D3D11_SUBRESOURCE_DATA init = { bytes, 0, 0 };

    T_CASE("WRITE_DISCARD of a dynamic buffer: S_OK, a 16-byte aligned pointer, both pitches the buffer's size");
    D3D11_BUFFER_DESC dyn = { 1000, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *vb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &dyn, NULL, &vb), S_OK);
    memset(&m, 0xa5, sizeof m);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    T_REQUIRE(m.pData != NULL);
    T_EQ((uintptr_t)m.pData % 16, 0);
    T_EQ(m.RowPitch, 1000);
    T_EQ(m.DepthPitch, 1000);
    T_EQ(rt_nulldev_memory(vb)->mapped, 1);

    T_CASE("the block ends at a guard page");
    uint8_t *p = m.pData;
    memset(p, 0x11, 1000);                              /* every byte of the buffer is writable */
    p[1007] = 1;                                        /* and the rest of its last 16-byte unit (1000 rounds up to 1008) */
    T_TRAPS(p[1008] = 1, "");                           /* the next byte is the guard page: a fault, reported */
    T_TRAPS(T_EQ(p[1008], 0), "");

    T_CASE("what the game wrote is there at the next mapping");
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)vb, 0);
    D3D11_BUFFER_DESC stg = { 1024, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *sb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, &init, &sb), S_OK);     /* all of it the game's, by initial data */
    memset(&m, 0, sizeof m);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, D3D11_MAP_READ_WRITE, 0, &m), S_OK);
    T_TRUE(!memcmp(m.pData, bytes, 1024));
    T_EQ(m.RowPitch, 1024);
    T_EQ(m.DepthPitch, 1024);
    ((uint8_t *)m.pData)[700] = 0x99;                   /* every byte was the game's, so it still is */
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)sb, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, D3D11_MAP_READ, 0, &m), S_OK);
    T_EQ(((uint8_t *)m.pData)[700], 0x99);
    T_EQ(((uint8_t *)m.pData)[701], bytes[701]);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)sb, 0);

    T_CASE("what the game writes through the pointer of a writing Map is kept and is not counted as written");
    /* docs/reviews/night-2026-10-11-code-review.md, finding 3: the device cannot see which
     * bytes the game writes through the pointer, so a subresource nothing else wrote stays
     * unwritten (here the game wrote 16 of 1024 bytes), and reading it is a stop. */
    ID3D11Buffer *half = NULL, *from_dyn = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &half), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)half, 0, D3D11_MAP_WRITE, 0, &m), S_OK);
    memset(m.pData, 0x42, 16);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)half, 0);
    T_EQ(rt_nulldev_block(half, 0)->data[15], 0x42);
    T_EQ(rt_nulldev_content(half, 0), ND_CONTENT_NONE);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)half, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource no call wrote");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)half, 0, D3D11_MAP_READ_WRITE, 0, &m), "a read mapping of a subresource no call wrote");
    D3D11_BOX first16 = { 0, 0, 0, 16, 1, 1 };          /* the same 16 bytes by a call the device sees: 16 of 1024 */
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)half, 0, &first16, bytes, 0, 0);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)half, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource of which calls wrote only a part (subresource 0: 16 of 1024 units");
    /* WRITE_DISCARD: "the previous contents of the resource will be undefined" (D3D11_MAP), so a
     * dynamic buffer created with data holds nothing the device can vouch for after one */
    D3D11_BUFFER_DESC dyn1024 = { 1024, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *dvb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &dyn1024, &init, &dvb), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &from_dyn), S_OK);
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)from_dyn, (ID3D11Resource *)dvb);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)from_dyn, 0, D3D11_MAP_READ, 0, &m), S_OK);
    T_TRUE(!memcmp(m.pData, bytes, 1024));              /* the initial data, copied */
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)from_dyn, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)dvb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    memset(m.pData, 0x24, 16);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)dvb, 0);
    T_EQ(rt_nulldev_content(dvb, 0), ND_CONTENT_NONE);
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)from_dyn, (ID3D11Resource *)dvb);
    T_EQ(rt_nulldev_block(from_dyn, 0)->data[15], 0x24);        /* the bytes are copied all the same */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)from_dyn, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource no call wrote");

    T_CASE("a read mapping of a staging buffer returns its initial data");
    ID3D11Buffer *ib = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, &init, &ib), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)ib, 0, D3D11_MAP_READ, 0, &m), S_OK);
    T_EQ((uintptr_t)m.pData % 16, 0);
    T_TRUE(!memcmp(m.pData, bytes, 1024));

    T_CASE("a mip level of a texture 2D gets the pitches of that level");
    D3D11_TEXTURE2D_DESC big = { 512, 512, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0,
                                 D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *t512 = NULL, *t8 = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &big, NULL, &t512), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)t512, 0, D3D11_MAP_WRITE, 0, &m), S_OK);
    T_EQ(m.RowPitch, 2048);
    T_EQ(m.DepthPitch, 1048576);
    ((uint8_t *)m.pData)[1048575] = 1;
    big.Width = big.Height = 8; big.MipLevels = 2;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &big, NULL, &t8), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)t8, 1, D3D11_MAP_WRITE, 0, &m), S_OK);
    T_EQ(m.RowPitch, 16);
    T_EQ(m.DepthPitch, 64);
    D3D11_TEXTURE3D_DESC cube = { 64, 64, 64, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture3D *t64 = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &cube, NULL, &t64), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)t64, 0, D3D11_MAP_WRITE, 0, &m), S_OK);
    T_EQ(m.RowPitch, 256);
    T_EQ(m.DepthPitch, 16384);

    T_CASE("a BC1 texture of 10 by 6 has RowPitch 24 and DepthPitch 48");
    D3D11_TEXTURE2D_DESC bc = { 10, 6, 1, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE,
                                D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *tbc = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &bc, NULL, &tbc), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)tbc, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    T_EQ(m.RowPitch, 24);
    T_EQ(m.DepthPitch, 48);

    T_CASE("WRITE_NO_OVERWRITE of a dynamic vertex buffer keeps its bytes");
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)vb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m), S_OK);
    T_TRUE(m.pData == p);
    for (int i = 0; i < 1000; i++) T_EQ(p[i], 0x11);

    /* vb is mapped; sb is not; ib is mapped for reading */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, NULL, 0, D3D11_MAP_READ, 0, &m), "Map of a NULL resource");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, D3D11_MAP_READ, 0, NULL), "Map with a NULL pMappedResource");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m), "Map with flags 0x100000");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 1, D3D11_MAP_READ, 0, &m), "Map of a subresource the resource does not have: 1 of 1");
    D3D11_TEXTURE2D_DESC ms = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture2D *msaa = NULL, *def = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &msaa), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)msaa, 0, D3D11_MAP_READ, 0, &m), "Map of a multisampled texture");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map of a subresource that is already mapped");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, 6, 0, &m), "a map type that is not one of D3D11_MAP");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, 0, 0, &m), "a map type that is not one of D3D11_MAP");
    ms.SampleDesc.Count = 1;                            /* a DEFAULT texture: no CPU access, every map type is refused */
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &def), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)def, 0, D3D11_MAP_READ, 0, &m), "D3D11_MAP_READ of a resource created without D3D11_CPU_ACCESS_READ");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)def, 0, D3D11_MAP_READ_WRITE, 0, &m), "D3D11_MAP_READ of a resource created without D3D11_CPU_ACCESS_READ");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)def, 0, D3D11_MAP_WRITE, 0, &m), "of a resource created without D3D11_CPU_ACCESS_WRITE");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)def, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "of a resource created without D3D11_CPU_ACCESS_WRITE");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)def, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m), "of a resource created without D3D11_CPU_ACCESS_WRITE");
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)vb, 0);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)vb, 0, D3D11_MAP_WRITE, 0, &m), "D3D11_MAP_WRITE of a dynamic resource");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "D3D11_MAP_WRITE_DISCARD of a resource that is not dynamic");
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)tbc, 0);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)tbc, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m), "D3D11_MAP_WRITE_NO_OVERWRITE of anything but a dynamic vertex or index buffer");
    dyn.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer *cb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &dyn, NULL, &cb), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m), "D3D11_MAP_WRITE_NO_OVERWRITE of anything but a dynamic vertex or index buffer");
    ID3D11Buffer *fresh = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, NULL, &fresh), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)fresh, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource no call wrote");
    rt_nulldev_memory(fresh)->content[0] = ND_CONTENT_DROPPED;  /* what a copy of bytes the device did not keep records */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)fresh, 0, D3D11_MAP_READ, 0, &m), "whose contents are the game's own bytes, copied from a resource whose bytes the device did not keep");
    rt_nulldev_rendered(sb, 0);                         /* what a copy from a render target into it records */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)sb, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource that a copy from a renderer's output or a resolve wrote");
    D3D11_TEXTURE2D_DESC huge = { 16384, 16384, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *widest = NULL;                        /* a slice of 2^32 bytes: the largest feature level 11_0 allows */
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &huge, NULL, &widest), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)widest, 0, D3D11_MAP_WRITE, 0, &m), "does not fit DepthPitch's 32 bits");
}
