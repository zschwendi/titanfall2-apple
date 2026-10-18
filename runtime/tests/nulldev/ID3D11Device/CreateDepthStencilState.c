/* Expected values: Microsoft's page of the creator (an equal state gives the same interface)
 * and Wine's d3d11 conformance tests as the contract's check reads them (docs/reviews/
 * d3d-contract-check.md, E5): what Windows normalises before it compares, and that the second
 * create returns the same object with one more reference.  The counts: a new device has 2
 * references and each object it makes holds it (docs/handoff/d3d-contract.md, section 3). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateDepthStencilState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    /* depth on, write ALL (1), LESS_EQUAL (4); stencil on, masks 0x0f and 0xf0, faces KEEP KEEP REPLACE (3) ALWAYS (8) */
    D3D11_DEPTH_STENCIL_DESC d, same, off, bad;
    ID3D11DepthStencilState *a = NULL, *b = NULL, *c = NULL, *e = NULL;
    memset(&d, 0xcc, sizeof d);
    d.DepthEnable = TRUE; d.DepthWriteMask = 1; d.DepthFunc = 4;
    d.StencilEnable = TRUE; d.StencilReadMask = 0x0f; d.StencilWriteMask = 0xf0;
    d.FrontFace = d.BackFace = (D3D11_DEPTH_STENCILOP_DESC){ 1, 1, 3, 8 };

    T_CASE("a new depth-stencil state: S_OK, one reference, the device held, the normalised description kept");
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &d, &a), S_OK);
    T_REQUIRE(a != NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(device), 3);
    const D3D11_DEPTH_STENCIL_DESC *k = &((NdStateObject *)rt_com_state_of(a, &rt_nulldev_depthstencil_class))->desc.depth_stencil;
    T_EQ(k->DepthFunc, 4);
    T_EQ(k->StencilReadMask, 0x0f);
    T_EQ(k->StencilWriteMask, 0xf0);
    T_EQ(k->BackFace.StencilPassOp, 3);

    T_CASE("an equal description gives the same object with one more reference");
    memset(&same, 0x33, sizeof same);
    same.DepthEnable = TRUE; same.DepthWriteMask = 1; same.DepthFunc = 4;
    same.StencilEnable = TRUE; same.StencilReadMask = 0x0f; same.StencilWriteMask = 0xf0;
    same.FrontFace = same.BackFace = (D3D11_DEPTH_STENCILOP_DESC){ 1, 1, 3, 8 };
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &same, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("with depth and stencil disabled a description differing only in unused fields is the same object");
    memset(&off, 0, sizeof off);                        /* everything 0: no enumerator is valid, and none is used */
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &off, &c), S_OK);
    k = &((NdStateObject *)rt_com_state_of(c, &rt_nulldev_depthstencil_class))->desc.depth_stencil;
    T_EQ(k->DepthWriteMask, 1);                         /* ALL */
    T_EQ(k->DepthFunc, 2);                              /* LESS */
    T_EQ(k->StencilReadMask, 0xff);
    T_EQ(k->StencilWriteMask, 0xff);
    T_EQ(k->FrontFace.StencilFailOp, 1);                /* KEEP, KEEP, KEEP, ALWAYS */
    T_EQ(k->FrontFace.StencilDepthFailOp, 1);
    T_EQ(k->FrontFace.StencilPassOp, 1);
    T_EQ(k->FrontFace.StencilFunc, 8);
    T_EQ(k->BackFace.StencilFunc, 8);
    off.DepthFunc = 7; off.StencilReadMask = 3; off.FrontFace.StencilFailOp = 5;
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &off, &e), S_OK);
    T_TRUE(e == c);
    T_EQ(rt_com_refs(c), 2);

    T_CASE("a different description gives another object");
    T_TRUE(c != a);
    same.StencilWriteMask = 0xff;
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &same, &e), S_OK);
    T_TRUE(e != a && e != c);

    T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &d, NULL), "a state creator with a NULL description or a NULL out-pointer");
    bad = d; bad.DepthFunc = 9;
    T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &bad, &b), "a depth function that is not one of D3D11_COMPARISON_FUNC");
}
