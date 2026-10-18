/* The forward of ID3D11Device::CreateDepthStencilState through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object
 * and the game's description by pointer, unchanged.  Rule 3: a new state object gets a back object
 * from the creator's out-pointer, and its last Release releases that object once.  Rule 7: a state
 * object that already exists (an equal description, as the nulldev test's CreateDepthStencilState
 * has it) is answered by the front alone: the back end is not asked.  Rule 4: a result of the back
 * end that differs from the front's S_OK is a stop that names the method.  Rule 1: a NULL
 * description, a NULL out-pointer and a depth function that is not one of D3D11_COMPARISON_FUNC
 * stop before the back end is reached. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateDepthStencilState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    /* depth on, write ALL (1), LESS_EQUAL (4); stencil on, masks 0x0f and 0xf0, faces KEEP KEEP REPLACE (3) ALWAYS (8) */
    D3D11_DEPTH_STENCIL_DESC d, same, e;
    memset(&d, 0xcc, sizeof d);                         /* padding bytes of the game's stack must not matter */
    d.DepthEnable = TRUE; d.DepthWriteMask = 1; d.DepthFunc = 4;
    d.StencilEnable = TRUE; d.StencilReadMask = 0x0f; d.StencilWriteMask = 0xf0;
    d.FrontFace = d.BackFace = (D3D11_DEPTH_STENCILOP_DESC){ 1, 1, 3, 8 };
    ID3D11DepthStencilState *a = NULL, *b = NULL, *c = NULL;

    T_CASE("a new depth-stencil state: S_OK, one reference, the device held, and the back end is asked once with the game's description by pointer");
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &d, &a), S_OK);
    T_REQUIRE(a != NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(device), 3);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateDepthStencilState), 1);
        T_EQ(T_BACK_ARG(CreateDepthStencilState, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateDepthStencilState, 1), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateDepthStencilState, 2), T_BACK_OF(a));
        T_TRUE(T_BACK_OF(a) != 0);
    }

    T_CASE("the same description gives the same object, and the front answers alone: the back end is not asked again");
    memset(&same, 0x33, sizeof same);                   /* other padding, other unused fields: the same state */
    same.DepthEnable = TRUE; same.DepthWriteMask = 1; same.DepthFunc = 4;
    same.StencilEnable = TRUE; same.StencilReadMask = 0x0f; same.StencilWriteMask = 0xf0;
    same.FrontFace = same.BackFace = (D3D11_DEPTH_STENCILOP_DESC){ 1, 1, 3, 8 };
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &same, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateDepthStencilState), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateDepthStencilState), 1);
    }

    T_CASE("a different description (stencil write mask) gives another object, with its own back object");
    e = d;
    e.StencilWriteMask = 0xff;
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &e, &c), S_OK);
    T_TRUE(c != a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateDepthStencilState), 2);
        T_EQ(T_BACK_ARG(CreateDepthStencilState, 1), T_BACK_PTR(&e));
        T_EQ(T_BACK_ARG(CreateDepthStencilState, 2), T_BACK_OF(c));
        T_TRUE(T_BACK_OF(c) != 0 && T_BACK_OF(c) != T_BACK_OF(a));
    }

    T_CASE("a Release that leaves a reference keeps the object; the last one releases its back object, once");
    uint64_t before = 0;
    if (T_BACK) before = T_BACK_COUNT(Release);
    T_EQ(COM_CALL(ID3D11DepthStencilState, Release, a), 1);      /* a and b are one object: 2 -> 1 */
    if (T_BACK) T_EQ(T_BACK_COUNT(Release), before);
    T_EQ(COM_CALL(ID3D11DepthStencilState, Release, b), 0);      /* 1 -> 0: released once */
    if (T_BACK) T_EQ(T_BACK_COUNT(Release), before + 1);
    before = 0;
    if (T_BACK) before = T_BACK_COUNT(Release);
    T_EQ(COM_CALL(ID3D11DepthStencilState, Release, c), 0);      /* the other description's object: 1 -> 0 */
    if (T_BACK) T_EQ(T_BACK_COUNT(Release), before + 1);

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        D3D11_DEPTH_STENCIL_DESC f = d;
        f.DepthFunc = 2;                                /* LESS: a third, new description */
        ID3D11DepthStencilState *third = NULL;
        rt_d3dback_record_answer(RT_BACK_CreateDepthStencilState, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &f, &third),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateDepthStencilState");
        rt_d3dback_record_answer(RT_BACK_CreateDepthStencilState, S_OK);
    }

    T_CASE("a NULL description, a NULL out-pointer or a depth function that is not one of D3D11_COMPARISON_FUNC stops before the back end is reached");
    ID3D11DepthStencilState *scratch = NULL;
    D3D11_DEPTH_STENCIL_DESC bad = d;
    bad.DepthFunc = 9;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilState, device, NULL, &scratch));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &d, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilState, device, &bad, &scratch));
}
