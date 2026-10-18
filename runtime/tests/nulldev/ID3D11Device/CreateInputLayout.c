/* Expected values: Microsoft's page of CreateInputLayout (S_OK; a NULL out-pointer validates:
 * S_FALSE).  The layout is a record of its elements (rt/nulldev.h): a copy, 32 bytes each
 * (D3D11_INPUT_ELEMENT_DESC's Windows layout, rt/win/d3d11.h). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateInputLayout)
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

    T_CASE("three elements: S_OK, one reference, the device held, the elements kept as a copy");
    T_EQ(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, sizeof code, &layout), S_OK);
    T_REQUIRE(layout != NULL);
    T_EQ(rt_com_refs(layout), 1);
    T_EQ(rt_com_refs(device), 3);
    const NdInputLayout *l = rt_com_state_of(layout, &rt_nulldev_inputlayout_class);
    T_TRUE(l->child.device == device);
    T_EQ(l->nelements, 3);
    T_TRUE(l->elements != el);
    T_TRUE(!memcmp(l->elements, el, 3 * sizeof el[0]));
    T_EQ(rt_com_release(layout), 0);
    T_EQ(rt_com_refs(device), 2);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    T_EQ(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, sizeof code, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 2);

    T_TRAPS(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 33, code, sizeof code, &layout), "CreateInputLayout with 33 elements");
    T_TRAPS(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 3, code, 2, &layout), "CreateInputLayout with bytecode");
}
