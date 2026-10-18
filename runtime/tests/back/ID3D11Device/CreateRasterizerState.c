/* The forward of ID3D11Device::CreateRasterizerState through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object
 * and the game's description by pointer, unchanged.  Rule 3: a new state object gets a back object
 * from the creator's out-pointer, and its last Release releases that object once.  Rule 7: a state
 * object that already exists (an equal description, as the nulldev test's CreateRasterizerState has
 * it) is answered by the front alone: the back end is not asked.  Rule 4: a result of the back end
 * that differs from the front's S_OK is a stop that names the method.  Rule 1: a NULL description,
 * a NULL out-pointer, a fill mode that is not one of D3D11_FILL_MODE and a cull mode that is not one
 * of D3D11_CULL_MODE stop before the back end is reached. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateRasterizerState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    /* SOLID (3), cull BACK (3), clockwise front, no bias, depth clip on */
    D3D11_RASTERIZER_DESC d, same, e;
    memset(&d, 0xcc, sizeof d);                         /* padding bytes of the game's stack must not matter */
    d.FillMode = 3; d.CullMode = 3; d.FrontCounterClockwise = FALSE; d.DepthBias = 0;
    d.DepthBiasClamp = 0.0f; d.SlopeScaledDepthBias = 0.0f; d.DepthClipEnable = TRUE;
    d.ScissorEnable = FALSE; d.MultisampleEnable = FALSE; d.AntialiasedLineEnable = FALSE;
    ID3D11RasterizerState *a = NULL, *b = NULL, *c = NULL;

    T_CASE("a new rasterizer state: S_OK, one reference, the device held, and the back end is asked once with the game's description by pointer");
    T_EQ(COM_CALL(ID3D11Device, CreateRasterizerState, device, &d, &a), S_OK);
    T_REQUIRE(a != NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(device), 3);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateRasterizerState), 1);
        T_EQ(T_BACK_ARG(CreateRasterizerState, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateRasterizerState, 1), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateRasterizerState, 2), T_BACK_OF(a));
        T_TRUE(T_BACK_OF(a) != 0);
    }

    T_CASE("the same description gives the same object, and the front answers alone: the back end is not asked again");
    memset(&same, 0x33, sizeof same);                   /* other padding: the same state */
    same.FillMode = 3; same.CullMode = 3; same.FrontCounterClockwise = FALSE; same.DepthBias = 0;
    same.DepthBiasClamp = 0.0f; same.SlopeScaledDepthBias = 0.0f; same.DepthClipEnable = TRUE;
    same.ScissorEnable = FALSE; same.MultisampleEnable = FALSE; same.AntialiasedLineEnable = FALSE;
    T_EQ(COM_CALL(ID3D11Device, CreateRasterizerState, device, &same, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateRasterizerState), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateRasterizerState), 1);
    }

    T_CASE("a different description (cull mode NONE) gives another object, with its own back object");
    e = d;
    e.CullMode = 1;                                     /* NONE */
    T_EQ(COM_CALL(ID3D11Device, CreateRasterizerState, device, &e, &c), S_OK);
    T_TRUE(c != a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateRasterizerState), 2);
        T_EQ(T_BACK_ARG(CreateRasterizerState, 1), T_BACK_PTR(&e));
        T_EQ(T_BACK_ARG(CreateRasterizerState, 2), T_BACK_OF(c));
        T_TRUE(T_BACK_OF(c) != 0 && T_BACK_OF(c) != T_BACK_OF(a));
    }

    T_CASE("a Release that leaves a reference keeps the object; the last one releases its back object, once");
    uint64_t before = 0;
    if (T_BACK) before = T_BACK_COUNT(Release);
    T_EQ(COM_CALL(ID3D11RasterizerState, Release, a), 1);        /* a and b are one object: 2 -> 1 */
    if (T_BACK) T_EQ(T_BACK_COUNT(Release), before);
    T_EQ(COM_CALL(ID3D11RasterizerState, Release, b), 0);        /* 1 -> 0: released once */
    if (T_BACK) T_EQ(T_BACK_COUNT(Release), before + 1);
    before = 0;
    if (T_BACK) before = T_BACK_COUNT(Release);
    T_EQ(COM_CALL(ID3D11RasterizerState, Release, c), 0);        /* the other description's object: 1 -> 0 */
    if (T_BACK) T_EQ(T_BACK_COUNT(Release), before + 1);

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        D3D11_RASTERIZER_DESC f = d;
        f.FillMode = 2;                                 /* WIREFRAME: a third, new description */
        ID3D11RasterizerState *third = NULL;
        rt_d3dback_record_answer(RT_BACK_CreateRasterizerState, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateRasterizerState, device, &f, &third),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateRasterizerState");
        rt_d3dback_record_answer(RT_BACK_CreateRasterizerState, S_OK);
    }

    T_CASE("a NULL description, a NULL out-pointer, a fill mode or a cull mode outside its enumeration stops before the back end is reached");
    ID3D11RasterizerState *scratch = NULL;
    D3D11_RASTERIZER_DESC bad = d;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateRasterizerState, device, NULL, &scratch));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateRasterizerState, device, &d, NULL));
    bad.FillMode = 1;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateRasterizerState, device, &bad, &scratch));
    bad = d;
    bad.CullMode = 0;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateRasterizerState, device, &bad, &scratch));
}
