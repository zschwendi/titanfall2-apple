/* The forward of ID3D11Device::CreatePixelShader through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object,
 * and the bytecode crosses as the game's own pointer, unchanged, with its length.  Rule 3: the
 * shader gets a back object of its own (the creator's out-pointer), and its destruction releases
 * it, once.  Rule 7: a call that only validates (a NULL out-pointer: S_FALSE) makes no shader and
 * is answered by the front alone.  Rule 4: a result of the back end that differs from the front's
 * S_OK is a stop that names the method.  Rule 1: each stop of the body (a class linkage, NULL bytes,
 * fewer than four bytes, bytes that do not begin with DXBC) comes before the back end is reached.
 * The front's own values (reference counts, the length kept) are those of the nulldev test, which
 * follows Microsoft's pages.  The bytes are not a real shader: only their first four characters
 * are looked at. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreatePixelShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    uint8_t code[64];
    ID3D11PixelShader *shader = NULL;
    memset(code, 0x7e, sizeof code);
    memcpy(code, "DXBC", 4);

    T_CASE("bytecode of 64 bytes beginning with DXBC: S_OK, one reference, the device held, the length kept; the device, the bytes and the length cross as the game passed them, the shader gets its own back object");
    T_EQ(COM_CALL(ID3D11Device, CreatePixelShader, device, code, sizeof code, NULL, &shader), S_OK);
    T_REQUIRE(shader != NULL);
    T_EQ(rt_com_refs(shader), 1);
    T_EQ(rt_com_refs(device), 3);
    const NdShader *s = rt_com_state_of(shader, &rt_nulldev_ps_class);
    T_TRUE(s->child.device == device);
    T_EQ(s->bytecode_length, 64);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreatePixelShader), 1);
        T_EQ(T_BACK_ARG(CreatePixelShader, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreatePixelShader, 1), T_BACK_PTR(code));
        T_EQ(T_BACK_ARG(CreatePixelShader, 2), 64);
        T_EQ(T_BACK_ARG(CreatePixelShader, 3), T_BACK_OF(shader));
        T_TRUE(T_BACK_OF(shader) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(shader)), RT_BK_PS);
    }

    T_CASE("a call that only validates (NULL out-pointer) returns S_FALSE: no shader, and the front answers alone");
    T_EQ(COM_CALL(ID3D11Device, CreatePixelShader, device, code, sizeof code, NULL, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 3);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreatePixelShader), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreatePixelShader), 1);
    }

    T_CASE("the shader's Release releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t live = T_BACK ? rt_d3dback_record_live(RT_BK_PS) : 0;
    T_EQ(COM_CALL(ID3D11PixelShader, Release, shader), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(rt_d3dback_record_live(RT_BK_PS), live - 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        ID3D11PixelShader *again = NULL;
        rt_d3dback_record_answer(RT_BACK_CreatePixelShader, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreatePixelShader, device, code, sizeof code, NULL, &again),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreatePixelShader");
        rt_d3dback_record_answer(RT_BACK_CreatePixelShader, S_OK);
    }

    T_CASE("each stop of the body comes before the back end is reached: a class linkage, NULL bytes, fewer than four bytes, bytes not beginning with DXBC");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreatePixelShader, device, code, sizeof code, (ID3D11ClassLinkage *)code, &shader));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreatePixelShader, device, NULL, 64, NULL, &shader));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreatePixelShader, device, code, 3, NULL, &shader));
    code[0] = 'd';
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreatePixelShader, device, code, sizeof code, NULL, &shader));
}
