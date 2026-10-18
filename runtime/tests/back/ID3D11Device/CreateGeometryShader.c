/* The forward of ID3D11Device::CreateGeometryShader through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object;
 * the bytecode crosses by pointer and length, unchanged (the game's memory, valid for the call).
 * Rule 3: the shader gets a back object from the creator's out-pointer, and its destruction
 * releases it, once.  Rule 7: a call that only validates (a NULL out-pointer: S_FALSE) makes no
 * shader and is answered by the front alone.  Rule 4: a result of the back end that differs from
 * the front's S_OK is a stop that names the method.  Rule 1: every stop of the body comes before
 * the back end is reached: a class linkage, bytecode that is NULL, shorter than 4 bytes or does
 * not begin with DXBC.  The bytecode is 64 bytes beginning with DXBC, as the nulldev test's
 * CreateGeometryShader passes it. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateGeometryShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    uint8_t code[64], bad[64];
    memset(code, 0x7e, sizeof code);
    memcpy(code, "DXBC", 4);
    memset(bad, 0x7e, sizeof bad);                      /* not DXBC: the first four bytes are not */
    ID3D11GeometryShader *shader = NULL, *second = NULL;

    T_CASE("bytecode of 64 bytes beginning with DXBC: S_OK, one reference, the device held, the length kept");
    T_EQ(COM_CALL(ID3D11Device, CreateGeometryShader, device, code, sizeof code, NULL, &shader), S_OK);
    T_REQUIRE(shader != NULL);
    T_EQ(rt_com_refs(shader), 1);
    const NdShader *s = rt_com_state_of(shader, &rt_nulldev_gs_class);
    T_TRUE(s->child.device == device);
    T_EQ(s->bytecode_length, 64);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateGeometryShader), 1);
        T_EQ(T_BACK_ARG(CreateGeometryShader, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateGeometryShader, 1), T_BACK_PTR(code));
        T_EQ(T_BACK_ARG(CreateGeometryShader, 2), 64);
        T_EQ(T_BACK_ARG(CreateGeometryShader, 3), T_BACK_OF(shader));
        T_TRUE(T_BACK_OF(shader) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(shader)), RT_BK_GS);
    }

    T_CASE("a call that only validates (NULL out-pointer): S_FALSE and no shader; the front answers alone and the back end is not asked");
    T_EQ(COM_CALL(ID3D11Device, CreateGeometryShader, device, code, sizeof code, NULL, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateGeometryShader), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateGeometryShader), 1);
    }

    T_CASE("the shader's Release releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t live = T_BACK ? rt_d3dback_record_live(RT_BK_GS) : 0;
    T_EQ(COM_CALL(ID3D11GeometryShader, Release, shader), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(rt_d3dback_record_live(RT_BK_GS), live - 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_CreateGeometryShader, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateGeometryShader, device, code, sizeof code, NULL, &second),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateGeometryShader");
        rt_d3dback_record_answer(RT_BACK_CreateGeometryShader, S_OK);
    }

    T_CASE("every stop of the body comes before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateGeometryShader, device, code, sizeof code, (ID3D11ClassLinkage *)code, &second));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateGeometryShader, device, NULL, 64, NULL, &second));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateGeometryShader, device, code, 3, NULL, &second));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateGeometryShader, device, bad, sizeof bad, NULL, &second));
}
