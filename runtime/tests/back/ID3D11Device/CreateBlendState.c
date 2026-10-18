/* The forward of ID3D11Device::CreateBlendState through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object
 * and the game's description by pointer, unchanged.  Rule 3: a new state object gets a back object
 * from the creator's out-pointer.  Rule 7: a state object that already exists (an equal
 * description, as the nulldev test's CreateBlendState has it) is answered by the front alone: the
 * back end is not asked.  Rule 4: a result of the back end that differs from the front's S_OK is a
 * stop that names the method.  Rule 1: a NULL description stops before the back end is reached. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateBlendState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BLEND_DESC d;
    memset(&d, 0xcc, sizeof d);                         /* padding bytes of the game's stack must not matter */
    d.AlphaToCoverageEnable = FALSE;
    d.IndependentBlendEnable = FALSE;
    for (int i = 0; i < 8; i++)                         /* SRC_ALPHA 5, INV_SRC_ALPHA 6, ADD 1, ONE 2, ZERO 1, write mask ALL */
        d.RenderTarget[i] = (D3D11_RENDER_TARGET_BLEND_DESC){ TRUE, 5, 6, 1, 2, 1, 1, 0x0f };
    ID3D11BlendState *a = NULL, *same = NULL, *other = NULL;

    T_CASE("a new blend state: S_OK, and the back end is asked once with the game's description by pointer");
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &d, &a), S_OK);
    T_REQUIRE(a != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateBlendState), 1);
        T_EQ(T_BACK_ARG(CreateBlendState, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateBlendState, 1), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateBlendState, 2), T_BACK_OF(a));
        T_TRUE(T_BACK_OF(a) != 0);
    }

    T_CASE("the same description gives the same object, and the front answers alone: the back end is not asked again");
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &d, &same), S_OK);
    T_TRUE(same == a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateBlendState), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateBlendState), 1);
    }

    T_CASE("a different description (render target 0's destination factor) gives another object, with its own back object");
    D3D11_BLEND_DESC e = d;
    e.RenderTarget[0].DestBlend = 2;                    /* ONE: valid, and the one target that counts when IndependentBlendEnable is FALSE */
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &e, &other), S_OK);
    T_TRUE(other != a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateBlendState), 2);
        T_EQ(T_BACK_ARG(CreateBlendState, 1), T_BACK_PTR(&e));
        T_EQ(T_BACK_ARG(CreateBlendState, 2), T_BACK_OF(other));
        T_TRUE(T_BACK_OF(other) != 0 && T_BACK_OF(other) != T_BACK_OF(a));
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        D3D11_BLEND_DESC f = d;
        f.RenderTarget[0].SrcBlend = 2;                 /* ONE: a third, new description */
        ID3D11BlendState *third = NULL;
        rt_d3dback_record_answer(RT_BACK_CreateBlendState, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, &f, &third),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateBlendState");
        rt_d3dback_record_answer(RT_BACK_CreateBlendState, S_OK);
    }

    T_CASE("a NULL description stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateBlendState, device, NULL, &a));
}
