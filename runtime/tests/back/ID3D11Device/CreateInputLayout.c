/* The forward of ID3D11Device::CreateInputLayout through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 3: the creator makes the front layout,
 * asks the back end for its own (entry CreateInputLayout: the back end's device, then the game's
 * elements, their number, the game's bytecode and its length, all unchanged) and keeps it; the
 * layout's destruction releases it, once.  Rule 7: a call that only validates (a NULL
 * out-pointer: S_FALSE, CreateInputLayout's page) makes no object and is answered by the front
 * alone.  Rule 4: a result of the back end that differs from the front's S_OK is a stop that
 * names the method and both results.  Rule 1: every stop of the body comes before the back end.
 * E_FAIL is 0x80004005 (winerror.h). */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateInputLayout)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    uint8_t code[64];
    memset(code, 0x7e, sizeof code);
    memcpy(code, "DXBC", 4);
    D3D11_INPUT_ELEMENT_DESC el[33];
    memset(el, 0, sizeof el);
    el[0] = (D3D11_INPUT_ELEMENT_DESC){ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, 0, 0 };
    el[1] = (D3D11_INPUT_ELEMENT_DESC){ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, 0, 0 };
    el[2] = (D3D11_INPUT_ELEMENT_DESC){ "TEXCOORD", 1, DXGI_FORMAT_R16G16_FLOAT, 1, 0, 0, 0 };
    ID3D11InputLayout *layout = NULL;

    T_CASE("three elements: S_OK, one reference, the device held, the elements kept as a copy; a back object from the game's elements and bytecode");
    T_EQ(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, sizeof code, &layout), S_OK);
    T_REQUIRE(layout != NULL);
    T_TRUE(rt_com_class_of(layout) == &rt_nulldev_inputlayout_class);
    T_EQ(rt_com_refs(layout), 1);
    T_EQ(rt_com_refs(device), 3);
    const NdInputLayout *l = rt_com_state_of(layout, &rt_nulldev_inputlayout_class);
    T_TRUE(l->child.device == device);
    T_EQ(l->nelements, 3);
    T_TRUE(l->elements != el);
    T_TRUE(!memcmp(l->elements, el, 3 * sizeof el[0]));
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateInputLayout), 1);
        T_EQ(T_BACK_ARG(CreateInputLayout, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateInputLayout, 1), T_BACK_PTR(el));
        T_EQ(T_BACK_ARG(CreateInputLayout, 2), 3);
        T_EQ(T_BACK_ARG(CreateInputLayout, 3), T_BACK_PTR(code));
        T_EQ(T_BACK_ARG(CreateInputLayout, 4), sizeof code);
        T_EQ(T_BACK_ARG(CreateInputLayout, 5), T_BACK_OF(layout));
        T_TRUE(T_BACK_OF(layout) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(layout)), RT_BK_INPUTLAYOUT);
        T_EQ(rt_d3dback_record_live(RT_BK_INPUTLAYOUT), 1);
    } else {
        T_TRUE(l->child.back == NULL);
    }

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object, so no back object is asked for");
    T_EQ(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, sizeof code, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 3);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateInputLayout), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateInputLayout), 1);
        T_EQ(rt_d3dback_record_made(RT_BK_INPUTLAYOUT), 1);
    }

    T_CASE("the last Release of the layout releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(layout) : 0;
    T_EQ(COM_CALL(ID3D11InputLayout, Release, layout), 0);
    T_EQ(rt_com_refs(device), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_INPUTLAYOUT);
        T_EQ(T_BACK_ARG(Release, 1), was);
        T_EQ(rt_d3dback_record_live(RT_BK_INPUTLAYOUT), 0);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names both");
        rt_d3dback_record_answer(RT_BACK_CreateInputLayout, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, sizeof code, &layout),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateInputLayout");
        rt_d3dback_record_answer(RT_BACK_CreateInputLayout, S_OK);
        T_EQ(T_BACK_COUNT(CreateInputLayout), 1);
    }

    T_CASE("every stop of the body comes before the back end");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 33, code, sizeof code, &layout));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateInputLayout, device, NULL, 3, code, sizeof code, &layout));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, NULL, sizeof code, &layout));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, 2, &layout));
    uint8_t other[64];
    memset(other, 0x7e, sizeof other);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, other, sizeof other, &layout));
}
