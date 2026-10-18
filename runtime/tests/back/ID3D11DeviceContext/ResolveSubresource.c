/* The forward of ID3D11DeviceContext::ResolveSubresource through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and both
 * resources cross as their back objects, NULL as NULL, the format by value.  Rule 1: every stop
 * comes before the back end is reached (a NULL resource, a source that is not multisampled, a
 * destination that is multisampled, a subresource out of range, an object of the wrong class). */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, ResolveSubresource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC ms = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_TEXTURE2D_DESC one = { 64, 64, 1, 2, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *src = NULL, *dst = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &src), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &one, NULL, &dst), S_OK);
    T_REQUIRE(src != NULL && dst != NULL);
    T_BACK_ADOPT(src);
    T_BACK_ADOPT(dst);

    T_CASE("a resolve is handed on once: both resources as their back objects, the format by value");
    COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)dst, 1, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ResolveSubresource), 1);
        T_EQ(T_BACK_ARG(ResolveSubresource, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(ResolveSubresource, 1), T_BACK_OF(dst));
        T_EQ(T_BACK_ARG(ResolveSubresource, 2), 1);
        T_EQ(T_BACK_ARG(ResolveSubresource, 3), T_BACK_OF(src));
        T_EQ(T_BACK_ARG(ResolveSubresource, 4), 0);
        T_EQ(T_BACK_ARG(ResolveSubresource, 5), DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    /* every stop of the body, before the back end */
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, NULL, 0, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)dst, 0, (ID3D11Resource *)dst, 1, DXGI_FORMAT_R8G8B8A8_UNORM));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)src, 0, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (ID3D11Resource *)dst, 2, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ResolveSubresource, context, (void *)T_BACK_NEW(device, &rt_nulldev_query_class), 0, (ID3D11Resource *)src, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
}
