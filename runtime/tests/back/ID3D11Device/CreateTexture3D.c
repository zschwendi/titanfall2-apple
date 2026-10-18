/* The forward of ID3D11Device::CreateTexture3D through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 3: the creator makes the front object,
 * asks the back end for its own (entry CreateTexture3D: the back end's device, then the game's
 * description and initial data by pointer, unchanged) and keeps it; the front object's
 * destruction releases it, once.  Rule 7: a call that only validates (a NULL out-pointer:
 * S_FALSE, the method's page) makes no object and is answered by the front alone.  Rule 4: a
 * result of the back end that differs from the front's S_OK is a stop that names the method and
 * both results.  Rule 1: the body's stops (a NULL description, a wrong class of device) and the
 * stops of the description and data (rt_nulldev_texture_new, rt_nulldev_fill) come before the
 * back end.  The device's own class check (rt_com_state) is not reached through COM_CALL: the
 * harness takes `this` by its vtable first (harness.c, rt_test_com_call), so no other class can
 * be passed as the device.  E_FAIL is 0x80004005 (winerror.h). */
#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateTexture3D)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE3D_DESC desc = { 32, 32, 4, 0, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_TEXTURE3D_DESC small = { 2, 2, 2, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
    uint8_t bytes[13];
    for (int i = 0; i < 13; i++) bytes[i] = (uint8_t)(i + 1);
    D3D11_SUBRESOURCE_DATA init = { bytes, 3, 8 };
    ID3D11Texture3D *texture = NULL;

    T_CASE("a new texture gets a back object from the game's description");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &desc, NULL, &texture), S_OK);
    T_REQUIRE(texture != NULL);
    T_TRUE(rt_com_class_of(texture) == &rt_nulldev_texture3d_class);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture3D), 1);
        T_EQ(T_BACK_ARG(CreateTexture3D, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateTexture3D, 1), T_BACK_PTR(&desc));
        T_EQ(T_BACK_ARG(CreateTexture3D, 2), 0);
        T_EQ(T_BACK_ARG(CreateTexture3D, 3), T_BACK_OF(texture));
        T_TRUE(T_BACK_OF(texture) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(texture)), RT_BK_TEXTURE3D);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE3D), 1);
    } else {
        T_TRUE(((NdTexture *)rt_com_state_of(texture, &rt_nulldev_texture3d_class))->child.back == NULL);
    }

    T_CASE("initial data crosses by pointer, unchanged");
    ID3D11Texture3D *filled = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &small, &init, &filled), S_OK);
    T_REQUIRE(filled != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture3D), 2);
        T_EQ(T_BACK_ARG(CreateTexture3D, 1), T_BACK_PTR(&small));
        T_EQ(T_BACK_ARG(CreateTexture3D, 2), T_BACK_PTR(&init));
        T_EQ(T_BACK_ARG(CreateTexture3D, 3), T_BACK_OF(filled));
        T_TRUE(T_BACK_OF(filled) != T_BACK_OF(texture));
    }

    T_CASE("a call that only validates makes no back object");
    T_EQ(COM_CALL(ID3D11Device, CreateTexture3D, device, &desc, NULL, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateTexture3D), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateTexture3D), 1);
        T_EQ(rt_d3dback_record_made(RT_BK_TEXTURE3D), 2);
    }

    T_CASE("the last Release of the texture releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(filled) : 0;
    T_EQ(COM_CALL(ID3D11Texture3D, Release, filled), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_TEXTURE3D);
        T_EQ(T_BACK_ARG(Release, 1), was);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE3D), 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's is a stop that names both");
        rt_d3dback_record_answer(RT_BACK_CreateTexture3D, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateTexture3D, device, &desc, NULL, &texture),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateTexture3D");
        rt_d3dback_record_answer(RT_BACK_CreateTexture3D, S_OK);
        T_EQ(T_BACK_COUNT(CreateTexture3D), 2);
    }

    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture3D, device, NULL, NULL, &texture));

    D3D11_TEXTURE3D_DESC bad = desc;
    bad.Depth = 0;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture3D, device, &bad, NULL, &texture));

    D3D11_TEXTURE3D_DESC wide = { 4096, 4, 4, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture3D, device, &wide, NULL, &texture));
    D3D11_TEXTURE3D_DESC deep = { 2048, 2048, 2049, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture3D, device, &deep, NULL, &texture));

    D3D11_SUBRESOURCE_DATA shorter = { bytes, 3, 4 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture3D, device, &small, &shorter, &texture));
    D3D11_SUBRESOURCE_DATA nobytes = { NULL, 3, 8 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateTexture3D, device, &small, &nobytes, &texture));
}
