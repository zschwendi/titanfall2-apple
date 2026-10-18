/* Expected values: Microsoft's page of the creator (an equal state gives the same interface)
 * and Wine's d3d11 conformance tests as the contract's check reads them (docs/reviews/
 * d3d-contract-check.md, E5): what Windows normalises before it compares, and that the second
 * create returns the same object with one more reference.  The counts: a new device has 2
 * references and each object it makes holds it (docs/handoff/d3d-contract.md, section 3). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateRasterizerState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    /* SOLID (3), cull BACK (3), clockwise front, no bias, depth clip on */
    D3D11_RASTERIZER_DESC d = { 3, 3, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE }, other, bad;
    ID3D11RasterizerState *a = NULL, *b = NULL, *c = NULL;

    T_CASE("a new rasterizer state: S_OK, one reference, the device held, the description kept");
    T_EQ(COM_CALL(ID3D11Device, CreateRasterizerState, device, &d, &a), S_OK);
    T_REQUIRE(a != NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(device), 3);
    const NdStateObject *s = rt_com_state_of(a, &rt_nulldev_rasterizer_class);
    T_TRUE(s->child.device == device);
    T_TRUE(!memcmp(&s->desc.rasterizer, &d, sizeof d));

    T_CASE("an equal description gives the same object with one more reference");
    other = d;
    T_EQ(COM_CALL(ID3D11Device, CreateRasterizerState, device, &other, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(device), 3);

    T_CASE("a different description gives another object");
    other.CullMode = 1;                                 /* NONE */
    T_EQ(COM_CALL(ID3D11Device, CreateRasterizerState, device, &other, &c), S_OK);
    T_TRUE(c != a);
    T_EQ(rt_com_refs(c), 1);
    T_EQ(rt_com_refs(device), 4);

    T_TRAPS(COM_CALL(ID3D11Device, CreateRasterizerState, device, NULL, &b), "a state creator with a NULL description or a NULL out-pointer");
    bad = d; bad.FillMode = 1;
    T_TRAPS(COM_CALL(ID3D11Device, CreateRasterizerState, device, &bad, &b), "a fill mode that is not one of D3D11_FILL_MODE");
    bad = d; bad.CullMode = 0;
    T_TRAPS(COM_CALL(ID3D11Device, CreateRasterizerState, device, &bad, &b), "a cull mode that is not one of D3D11_CULL_MODE");
}
