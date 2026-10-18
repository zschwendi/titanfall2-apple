/* The forward of ID3D11Device::CreateBuffer through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 3: the creator makes the front object,
 * asks the back end for its own (entry CreateBuffer: the back end's device, then the game's
 * description and initial data by pointer, unchanged) and keeps it; the front object's
 * destruction releases it, once.  Rule 7: a call that only validates (a NULL out-pointer:
 * S_FALSE, CreateBuffer's page) makes no object and is answered by the front alone.  Rule 4: a
 * result of the back end that differs from the front's S_OK is a stop that names the method and
 * both results.  Rule 1: the body's stop comes before the back end.  E_OUTOFMEMORY is
 * 0x8007000e (winerror.h). */
#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateBuffer)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    uint8_t bytes[64] = { 1, 2, 3 };
    D3D11_BUFFER_DESC desc = { .ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA init = { .pSysMem = bytes };
    ID3D11Buffer *buffer = NULL;

    T_CASE("a new buffer gets a back object from the game's description and data");
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, &init, &buffer), S_OK);
    T_REQUIRE(buffer != NULL);
    T_TRUE(rt_com_class_of(buffer) == &rt_nulldev_buffer_class);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateBuffer), 1);
        T_EQ(T_BACK_ARG(CreateBuffer, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateBuffer, 1), T_BACK_PTR(&desc));
        T_EQ(T_BACK_ARG(CreateBuffer, 2), T_BACK_PTR(&init));
        T_EQ(T_BACK_ARG(CreateBuffer, 3), T_BACK_OF(buffer));
        T_TRUE(T_BACK_OF(buffer) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(buffer)), RT_BK_BUFFER);
        T_EQ(rt_d3dback_record_live(RT_BK_BUFFER), 1);
    } else {
        T_TRUE(((NdBuffer *)rt_com_state_of(buffer, &rt_nulldev_buffer_class))->child.back == NULL);
    }

    T_CASE("without initial data the data crosses as NULL");
    ID3D11Buffer *second = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, &second), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateBuffer), 2);
        T_EQ(T_BACK_ARG(CreateBuffer, 2), 0);
        T_TRUE(T_BACK_OF(second) != T_BACK_OF(buffer));
    }

    T_CASE("a call that only validates makes no back object");
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateBuffer), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateBuffer), 1);
        T_EQ(rt_d3dback_record_made(RT_BK_BUFFER), 2);
    }

    T_CASE("the last Release of the buffer releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(second) : 0;
    T_EQ(COM_CALL(ID3D11Buffer, Release, second), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_BUFFER);
        T_EQ(T_BACK_ARG(Release, 1), was);
        T_EQ(rt_d3dback_record_live(RT_BK_BUFFER), 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's is a stop that names both");
        rt_d3dback_record_answer(RT_BACK_CreateBuffer, E_OUTOFMEMORY);
        T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, &second),
                "the back end (record) answered 0x8007000e to ID3D11Device::CreateBuffer, where the front answered 0x00000000");
        rt_d3dback_record_answer(RT_BACK_CreateBuffer, S_OK);
        T_EQ(T_BACK_COUNT(CreateBuffer), 2);
    }

    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateBuffer, device, NULL, NULL, &second));
    desc.ByteWidth = 0;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateBuffer, device, &desc, NULL, &second));
}
