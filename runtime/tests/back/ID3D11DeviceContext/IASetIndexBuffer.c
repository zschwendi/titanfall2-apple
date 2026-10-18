/* The forward of ID3D11DeviceContext::IASetIndexBuffer through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the buffer
 * cross as their back objects, NULL as NULL; the format and the offset cross as passed.  Rule 1:
 * every stop comes before the back end is reached.  The buffer is an argument, so an object of
 * another class is a stop too. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, IASetIndexBuffer)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Buffer *ib = T_BACK_NEW(device, &rt_nulldev_buffer_class);

    T_CASE("an index buffer is handed on as its back object, with its format and offset");
    COM_CALL(ID3D11DeviceContext, IASetIndexBuffer, context, ib, DXGI_FORMAT_R16_UINT, 12);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetIndexBuffer), 1);
        T_EQ(T_BACK_ARG(IASetIndexBuffer, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(IASetIndexBuffer, 1), T_BACK_OF(ib));
        T_TRUE(T_BACK_OF(ib) != T_BACK_PTR(ib));
        T_EQ(T_BACK_ARG(IASetIndexBuffer, 2), DXGI_FORMAT_R16_UINT);
        T_EQ(T_BACK_ARG(IASetIndexBuffer, 3), 12);
    }

    T_CASE("a NULL index buffer is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, IASetIndexBuffer, context, NULL, DXGI_FORMAT_R16_UINT, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetIndexBuffer), 2);
        T_EQ(T_BACK_ARG(IASetIndexBuffer, 1), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11Buffer *query_as_buffer = (ID3D11Buffer *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetIndexBuffer, context, query_as_buffer, DXGI_FORMAT_R16_UINT, 0));
}
