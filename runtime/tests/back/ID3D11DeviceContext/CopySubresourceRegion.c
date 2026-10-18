/* The forward of ID3D11DeviceContext::CopySubresourceRegion through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and both
 * resources cross as their back objects, the box by pointer, unchanged, NULL as NULL.  Rule 1:
 * every stop comes before the back end is reached (a NULL resource, a NULL source, a box that
 * reaches past the source, an object of the wrong class). */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, CopySubresourceRegion)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC desc = { .ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER };
    ID3D11Buffer *dst = NULL;
    ID3D11Buffer *src = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, &dst), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, &src), S_OK);
    T_REQUIRE(dst != NULL && src != NULL);

    T_CASE("a box copy is handed on once: both resources as their back objects, the box by pointer");
    D3D11_BOX box = { .left = 0, .top = 0, .front = 0, .right = 16, .bottom = 1, .back = 1 };
    COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)dst, 0, 8, 0, 0, (ID3D11Resource *)src, 0, &box);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CopySubresourceRegion), 1);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 1), T_BACK_OF(dst));
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 2), 0);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 3), 8);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 4), 0);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 5), 0);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 6), T_BACK_OF(src));
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 7), 0);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 8), T_BACK_PTR(&box));
    }

    T_CASE("a copy of the whole subresource (a NULL box) is handed on with a NULL box");
    COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)dst, 0, 0, 0, 0, (ID3D11Resource *)src, 0, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CopySubresourceRegion), 2);
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 1), T_BACK_OF(dst));
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 6), T_BACK_OF(src));
        T_EQ(T_BACK_ARG(CopySubresourceRegion, 8), 0);
    }

    /* every stop of the body, before the back end */
    D3D11_BOX far = { .left = 0, .top = 0, .front = 0, .right = 128, .bottom = 1, .back = 1 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, NULL, 0, 0, 0, 0, (ID3D11Resource *)src, 0, &box));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)dst, 0, 0, 0, 0, NULL, 0, &box));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)dst, 0, 0, 0, 0, (ID3D11Resource *)src, 0, &far));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (void *)T_BACK_NEW(device, &rt_nulldev_query_class), 0, 0, 0, 0, (ID3D11Resource *)src, 0, &box));
}
