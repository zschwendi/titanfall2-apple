/* Expected values: Microsoft's page of the method (the source must be multisampled, the
 * destination single-sampled); that the destination then holds what only a renderer could
 * have produced is the device's rule (rt/nulldev.h, ND_CONTENT_*). */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, ResolveSubresource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC ms = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_TEXTURE2D_DESC one = { 64, 64, 1, 2, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *src = NULL, *dst = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &src), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &one, NULL, &dst), S_OK);

    T_CASE("a resolve of a multisampled texture leaves the destination subresource a renderer's, and no other");
    COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)dst, 1, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    T_EQ(rt_nulldev_content(dst, 1), ND_CONTENT_RENDERER);
    T_EQ(rt_nulldev_content(dst, 0), ND_CONTENT_NONE);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, NULL, 0, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM), "ResolveSubresource with a NULL resource");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)dst, 0, (ID3D11Resource *)dst, 1, DXGI_FORMAT_R8G8B8A8_UNORM), "ResolveSubresource from a source that is not multisampled");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)src, 0, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM), "ResolveSubresource into a destination that is multisampled");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)dst, 2, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM), "ResolveSubresource of subresource");
}
