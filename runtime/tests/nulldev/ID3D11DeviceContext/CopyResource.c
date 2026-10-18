/* Expected values: Microsoft's page of the method (the entire contents; same type, identical
 * dimensions); the byte patterns are the test's own, one per mip level, so a level that landed
 * in the wrong subresource would show.  The read-back case is the recording's
 * (docs/trace/d3d-map.md: "CopyResource; to tex2d R32_FLOAT STAGING bind=0x0 cpu=READ; from
 * tex2d R32_TYPELESS DEFAULT bind=SHADER_RESOURCE | RENDER_TARGET", then 381 read mappings of
 * the 32 by 32 texture); that its contents are a renderer's and that reading them stops is
 * the device's rule (rt/nulldev.h, ND_CONTENT_*; docs/handoff/d3d-fix-2.md, item 3). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, CopyResource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_TEXTURE2D_DESC sd = { 8, 4, 3, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *a = NULL, *b = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &a), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &b), S_OK);
    static const UINT bytes[3] = { 8 * 4 * 4, 4 * 2 * 4, 2 * 1 * 4 };
    /* Written by UpdateSubresource, which the device sees: what the game writes through the
     * pointer of a writing Map does not count as written (Map.c; the night's review, finding 3). */
    static uint8_t fill[32 * 32 * 4];
    for (UINT k = 0; k < 3; k++) {
        memset(fill, 0x10 + (int)k, bytes[k]);
        COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)a, k, NULL, fill, (8u >> k) * 4, 0);
    }

    T_CASE("a staging texture the game wrote is copied whole, every mip level, into another");
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)b, (ID3D11Resource *)a);
    for (UINT k = 0; k < 3; k++) {
        T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)b, k, D3D11_MAP_READ, 0, &m), S_OK);
        const uint8_t *p = m.pData;
        T_EQ(p[0], 0x10 + k);
        T_EQ(p[bytes[k] - 1], 0x10 + k);
        COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)b, k);
    }

    T_CASE("the recording's read-back, a render target copied into a 32 by 32 staging texture: the copy is taken and the staging texture is a renderer's");
    D3D11_TEXTURE2D_DESC rt = { 32, 32, 1, 1, DXGI_FORMAT_R32_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_TEXTURE2D_DESC rb = { 32, 32, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
    D3D11_TEXTURE2D_DESC wr = { 32, 32, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *target = NULL, *back = NULL, *own = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &rt, NULL, &target), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &rb, NULL, &back), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &wr, NULL, &own), S_OK);
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)target);
    T_EQ(rt_nulldev_content(back, 0), ND_CONTENT_RENDERER);

    T_CASE("a later copy of the game's bytes over all of it makes it readable again");
    memset(fill, 0x3c, sizeof fill);
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)own, 0, NULL, fill, 32 * 4, 0);
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)own);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)back, 0, D3D11_MAP_READ, 0, &m), S_OK);
    T_EQ(m.RowPitch, 128);                              /* the recording's number for this texture */
    T_EQ(((const uint8_t *)m.pData)[32 * 32 * 4 - 1], 0x3c);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)back, 0);

    T_CASE("formats of one type group are copied, as bytes");
    /* Microsoft, CopyResource: "the formats must be identical or at least from the same type
     * group.  For example, a DXGI_FORMAT_R32G32B32_FLOAT texture can be copied to a
     * DXGI_FORMAT_R32G32B32_UINT texture"; here UNORM to UNORM_SRGB of R8G8B8A8. */
    D3D11_TEXTURE2D_DESC u8 = { 8, 8, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *unorm = NULL, *srgb = NULL, *f32 = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &u8, NULL, &unorm), S_OK);
    u8.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &u8, NULL, &srgb), S_OK);
    u8.Format = DXGI_FORMAT_R32_FLOAT;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &u8, NULL, &f32), S_OK);
    memset(fill, 0x6b, 8 * 8 * 4);
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)unorm, 0, NULL, fill, 32, 0);
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)srgb, (ID3D11Resource *)unorm);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)srgb, 0, D3D11_MAP_READ, 0, &m), S_OK);
    T_EQ(((const uint8_t *)m.pData)[8 * 8 * 4 - 1], 0x6b);
    /* srgb stays mapped for the stops below */

    /* the night's review, finding 8: R8G8B8A8_UNORM to R32_FLOAT, elements of the same size */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)f32, (ID3D11Resource *)unorm), "a copy between formats of different type groups (28 to 41)");
    T_EQ(rt_nulldev_content(f32, 0), ND_CONTENT_NONE);
    /* finding 9: Microsoft, CopyResource: source and destination "Can't be currently mapped" */
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)srgb, (ID3D11Resource *)unorm), "a copy into a resource that is currently mapped");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)unorm, (ID3D11Resource *)srgb), "a copy from a resource that is currently mapped");
    T_EQ(((const uint8_t *)m.pData)[0], 0x6b);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)srgb, 0);
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)unorm, (ID3D11Resource *)srgb);
    T_EQ(rt_nulldev_content(unorm, 0), ND_CONTENT_GAME);

    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)target);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)back, 0, D3D11_MAP_READ, 0, &m), "a read mapping of a subresource that a copy from a renderer's output or a resolve wrote");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)a, (ID3D11Resource *)a), "CopyResource of a resource onto itself");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)a), "CopyResource between resources of 3 and 1 subresources");
    D3D11_TEXTURE2D_DESC other = { 16, 16, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *o = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &other, NULL, &o), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)o), "CopyResource between resources of different dimensions");
    D3D11_BUFFER_DESC bd = { 4096, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buf), S_OK);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)buf), "a copy between resources of different type");
}
