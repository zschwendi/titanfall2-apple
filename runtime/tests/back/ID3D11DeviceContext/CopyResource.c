/* The forward of ID3D11DeviceContext::CopyResource through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and both
 * resources cross as their back objects, NULL as NULL.  Rule 1: every stop comes before the back
 * end is reached; a copy between buffers of different size is a stop of the front. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, CopyResource)
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

    T_CASE("a copy is handed on once, both resources as their back objects");
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)dst, (ID3D11Resource *)src);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CopyResource), 1);
        T_EQ(T_BACK_ARG(CopyResource, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(CopyResource, 1), T_BACK_OF(dst));
        T_EQ(T_BACK_ARG(CopyResource, 2), T_BACK_OF(src));
        T_TRUE(T_BACK_OF(dst) != T_BACK_OF(src));
    }

    /* every stop of the body, before the back end */
    D3D11_BUFFER_DESC small_desc = { .ByteWidth = 32, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER };
    ID3D11Buffer *small = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &small_desc, NULL, &small), S_OK);
    T_REQUIRE(small != NULL);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopyResource, context, NULL, (ID3D11Resource *)src));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)dst, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)dst, (ID3D11Resource *)small));
}
