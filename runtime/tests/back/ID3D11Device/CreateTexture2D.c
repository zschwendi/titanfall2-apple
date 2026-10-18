/* The forward of ID3D11Device::CreateTexture2D through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 3: the creator makes the front object,
 * asks the back end for its own (entry CreateTexture2D: the back end's device, then the game's
 * description and initial data by pointer, unchanged) and keeps it; the front object's
 * destruction releases it, once.  Rule 7: a call that only validates (a NULL out-pointer:
 * S_FALSE, CreateTexture2D's page) makes no object and is answered by the front alone.  Rule 4: a
 * result of the back end that differs from the front's S_OK is a stop that names the method and
 * both results.  Rule 1: the body's stops come before the back end.  E_FAIL is 0x80004005
 * (winerror.h). */
#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateTexture2D)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC small = { 2, 2, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
    uint8_t src[20];
    for (int i = 0; i < 20; i++) src[i] = (uint8_t)(i + 1);
    D3D11_SUBRESOURCE_DATA init = { src, 12, 0 };
    ID3D11Texture2D *texture = NULL;

    T_CASE("a new texture gets a back object from the game's description and data");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, &init, &texture), S_OK);
    T_REQUIRE(texture != NULL);
    T_TRUE(rt_com_class_of(texture) == &rt_nulldev_texture2d_class);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture2D), 1);
        T_EQ(T_BACK_ARG(CreateTexture2D, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateTexture2D, 1), T_BACK_PTR(&small));
        T_EQ(T_BACK_ARG(CreateTexture2D, 2), T_BACK_PTR(&init));
        T_EQ(T_BACK_ARG(CreateTexture2D, 3), T_BACK_OF(texture));
        T_TRUE(T_BACK_OF(texture) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(texture)), RT_BK_TEXTURE2D);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE2D), 1);
    } else {
        T_TRUE(((NdTexture *)rt_com_state_of(texture, &rt_nulldev_texture2d_class))->child.back == NULL);
    }

    T_CASE("without initial data the data crosses as NULL");
    ID3D11Texture2D *second = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, NULL, &second), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture2D), 2);
        T_EQ(T_BACK_ARG(CreateTexture2D, 2), 0);
        T_TRUE(T_BACK_OF(second) != T_BACK_OF(texture));
    }

    T_CASE("a call that only validates makes no back object");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, NULL, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture2D), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateTexture2D), 1);
        T_EQ(rt_d3dback_record_made(RT_BK_TEXTURE2D), 2);
    }

    T_CASE("the last Release of the texture releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(second) : 0;
    T_EQ(COM_CALL(ID3D11Texture2D, Release, second), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_TEXTURE2D);
        T_EQ(T_BACK_ARG(Release, 1), was);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE2D), 1);
    }

    ID3D11Texture2D *spare = NULL;
    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's is a stop that names both");
        rt_d3dback_record_answer(RT_BACK_CreateTexture2D, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, NULL, &spare),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateTexture2D");
        rt_d3dback_record_answer(RT_BACK_CreateTexture2D, S_OK);
        T_EQ(T_BACK_COUNT(CreateTexture2D), 2);
    }

    T_CASE("a stop of the body or of its description comes before the back end");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture2D, device, NULL, NULL, &spare));
    D3D11_TEXTURE2D_DESC bad = small;
    bad.Height = 0;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture2D, device, &bad, NULL, &spare));
    D3D11_SUBRESOURCE_DATA nobytes = { NULL, 12, 0 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture2D, device, &small, &nobytes, &spare));
    /* A wrong-class `this` is not tested here: COM_CALL reads the object's own vtable (rt/test.h),
     * so a query passed as the device never reaches the body (rt_test_com_call: "does not carry
     * the vtable of ID3D11Device"), and the body's own rt_com_state stop is not reachable this way. */
}
