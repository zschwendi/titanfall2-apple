/* The forward of ID3D11Device::CreateTexture1D through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 3: the creator makes the front object,
 * asks the back end for its own (entry CreateTexture1D: the back end's device, then the game's
 * description and initial data by pointer, unchanged) and keeps it; the front object's
 * destruction releases it, once.  Rule 7: a call that only validates (a NULL out-pointer:
 * S_FALSE, CreateTexture1D's page) makes no object and is answered by the front alone.  Rule 4: a
 * result of the back end that differs from the front's S_OK is a stop that names the method and
 * both results.  Rule 1: the body's stops come before the back end.  E_FAIL is 0x80004005
 * (winerror.h). */
#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateTexture1D)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE1D_DESC desc = { 32, 0, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    uint8_t bytes[6][32];
    D3D11_SUBRESOURCE_DATA init[6];
    for (int i = 0; i < 6; i++) {
        for (int x = 0; x < 32; x++) bytes[i][x] = (uint8_t)(16 * i + x + 1);
        init[i] = (D3D11_SUBRESOURCE_DATA){ bytes[i], 0, 0 };
    }
    ID3D11Texture1D *texture = NULL;

    T_CASE("a new texture gets a back object from the game's description and data");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &desc, init, &texture), S_OK);
    T_REQUIRE(texture != NULL);
    T_TRUE(rt_com_class_of(texture) == &rt_nulldev_texture1d_class);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture1D), 1);
        T_EQ(T_BACK_ARG(CreateTexture1D, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateTexture1D, 1), T_BACK_PTR(&desc));
        T_EQ(T_BACK_ARG(CreateTexture1D, 2), T_BACK_PTR(init));
        T_EQ(T_BACK_ARG(CreateTexture1D, 3), T_BACK_OF(texture));
        T_TRUE(T_BACK_OF(texture) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(texture)), RT_BK_TEXTURE1D);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE1D), 1);
    } else {
        T_TRUE(((NdTexture *)rt_com_state_of(texture, &rt_nulldev_texture1d_class))->child.back == NULL);
    }

    T_CASE("without initial data the data crosses as NULL");
    ID3D11Texture1D *second = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &desc, NULL, &second), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture1D), 2);
        T_EQ(T_BACK_ARG(CreateTexture1D, 2), 0);
        T_TRUE(T_BACK_OF(second) != T_BACK_OF(texture));
    }

    T_CASE("a call that only validates makes no back object");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture1D, device, &desc, NULL, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture1D), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateTexture1D), 1);
        T_EQ(rt_d3dback_record_made(RT_BK_TEXTURE1D), 2);
    }

    T_CASE("the last Release of the texture releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(second) : 0;
    T_EQ(COM_CALL(ID3D11Texture1D, Release, second), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_TEXTURE1D);
        T_EQ(T_BACK_ARG(Release, 1), was);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE1D), 1);
    }

    ID3D11Texture1D *spare = NULL;
    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's is a stop that names both");
        rt_d3dback_record_answer(RT_BACK_CreateTexture1D, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateTexture1D, device, &desc, NULL, &spare),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateTexture1D");
        rt_d3dback_record_answer(RT_BACK_CreateTexture1D, S_OK);
        T_EQ(T_BACK_COUNT(CreateTexture1D), 2);
    }

    T_CASE("a stop of the body or of its description comes before the back end");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture1D, device, NULL, NULL, &spare));
    D3D11_TEXTURE1D_DESC bad = desc;
    bad.Width = 0;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture1D, device, &bad, NULL, &spare));
    D3D11_SUBRESOURCE_DATA nobytes[6] = { { NULL, 0, 0 } };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture1D, device, &desc, nobytes, &spare));
    /* A wrong-class `this` is not tested here: COM_CALL reads the object's own vtable (rt/test.h),
     * so a query passed as the device never reaches the body (rt_test_com_call: "does not carry
     * the vtable of ID3D11Device"), and the body's own rt_com_state stop is not reachable this way. */
}
