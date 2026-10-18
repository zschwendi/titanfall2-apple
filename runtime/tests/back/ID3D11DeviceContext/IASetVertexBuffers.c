/* The forward of ID3D11DeviceContext::IASetVertexBuffers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each buffer cross as
 * their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's own); the
 * strides and offsets cross by pointer, unchanged; a NULL array as NULL; StartSlot and NumBuffers
 * as passed.  Rule 1: each of the body's stops comes before the back end is reached.  The slot
 * count is 32 (D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, rt/win/d3d11.h).  Nothing is recorded in
 * the front: NdContext is the same with and without a back end. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, IASetVertexBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Buffer *a = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11Buffer *b = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11SamplerState *sampler = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11Buffer const *three[3] = { a, NULL, b };
    UINT strides[3] = { 16, 20, 24 }, offsets[3] = { 0, 4, 8 };

    T_CASE("three slots with a NULL in the middle: each buffer crosses as its back object, NULL as NULL, strides and offsets by pointer");
    COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 4, 3, three, strides, offsets);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetVertexBuffers), 1);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 1), 4);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 2), 3);
        T_TRUE(T_BACK_ARG(IASetVertexBuffers, 3) != 0 && T_BACK_ARG(IASetVertexBuffers, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(IASetVertexBuffers, 3), 3);
        T_EQ(T_BACK_ELEMENT(IASetVertexBuffers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(IASetVertexBuffers, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(IASetVertexBuffers, 3, 2), T_BACK_OF(b));
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 4), T_BACK_PTR(strides));
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 5), T_BACK_PTR(offsets));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with three NULL arrays is handed on with NULLs");
    COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 2, 0, NULL, NULL, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetVertexBuffers), 2);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 1), 2);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 2), 0);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 3), 0);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 4), 0);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 5), 0);
    }

    T_CASE("the last slot, 31");
    COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 31, 1, three, strides, offsets);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetVertexBuffers), 3);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 1), 31);
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 2), 1);
        T_EQ(T_BACK_ELEMENT(IASetVertexBuffers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 4), T_BACK_PTR(strides));
        T_EQ(T_BACK_ARG(IASetVertexBuffers, 5), T_BACK_PTR(offsets));
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11Buffer const *wrong[1] = { (ID3D11Buffer const *)sampler };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 30, 3, three, strides, offsets));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 0, 1, NULL, strides, offsets));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 0, 1, three, NULL, offsets));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 0, 1, three, strides, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetVertexBuffers, context, 0, 1, wrong, strides, offsets));
}
