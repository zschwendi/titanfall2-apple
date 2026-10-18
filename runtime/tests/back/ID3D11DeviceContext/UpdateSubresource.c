/* The forward of ID3D11DeviceContext::UpdateSubresource through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the
 * resource cross as their back objects, NULL as NULL; the box and the game's bytes cross by
 * pointer, unchanged; the subresource and the pitches as passed.  Rule 1: every stop comes before
 * the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, UpdateSubresource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC desc = { .ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, &buf), S_OK);
    T_REQUIRE(buf != NULL);
    uint8_t data[64] = { 9, 8, 7 };

    T_CASE("a whole-buffer update is handed on once, the game's bytes by pointer");
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)buf, 0, NULL, data, 0, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(UpdateSubresource), 1);
        T_EQ(T_BACK_ARG(UpdateSubresource, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(UpdateSubresource, 1), T_BACK_OF(buf));
        T_EQ(T_BACK_ARG(UpdateSubresource, 2), 0);
        T_EQ(T_BACK_ARG(UpdateSubresource, 3), 0);
        T_EQ(T_BACK_ARG(UpdateSubresource, 4), T_BACK_PTR(data));
        T_EQ(T_BACK_ARG(UpdateSubresource, 5), 0);
        T_EQ(T_BACK_ARG(UpdateSubresource, 6), 0);
    }

    T_CASE("an update with a box is handed on with the box by pointer");
    D3D11_BOX box = { .left = 8, .top = 0, .front = 0, .right = 24, .bottom = 1, .back = 1 };
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)buf, 0, &box, data, 0, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(UpdateSubresource), 2);
        T_EQ(T_BACK_ARG(UpdateSubresource, 3), T_BACK_PTR(&box));
    }

    /* every stop of the body, before the back end */
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)buf, 0, NULL, NULL, 0, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, NULL, 0, NULL, data, 0, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)buf, 1, NULL, data, 0, 0));
}
