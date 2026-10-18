/* The forward of ID3D11DeviceContext::CSSetUnorderedAccessViews through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each view cross as
 * their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's own), a
 * NULL array as NULL; StartSlot and NumUAVs as passed; the initial counts cross by pointer,
 * unchanged, NULL as NULL.  Rule 1: each of the body's stops comes before the back end is reached.
 * Slot count: D3D11_PS_CS_UAV_REGISTER_COUNT (8), from the page's register range. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, CSSetUnorderedAccessViews)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11UnorderedAccessView *a = T_BACK_NEW(device, &rt_nulldev_uav_class);
    ID3D11UnorderedAccessView *b = T_BACK_NEW(device, &rt_nulldev_uav_class);
    ID3D11UnorderedAccessView const *two[2] = { a, b };
    UINT counts[2] = { 0, 0xffffffffu };

    T_CASE("two slots with initial counts: each view crosses as its back object, the counts by pointer");
    COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 1, 2, two, counts);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CSSetUnorderedAccessViews), 1);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 1), 1);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 2), 2);
        T_TRUE(T_BACK_ARG(CSSetUnorderedAccessViews, 3) != 0 && T_BACK_ARG(CSSetUnorderedAccessViews, 3) != T_BACK_PTR(two));
        T_EQ(T_BACK_ARRAY_LEN(CSSetUnorderedAccessViews, 3), 2);
        T_EQ(T_BACK_ELEMENT(CSSetUnorderedAccessViews, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(CSSetUnorderedAccessViews, 3, 1), T_BACK_OF(b));
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 4), T_BACK_PTR(counts));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("two slots with NULL initial counts: the NULL crosses as NULL");
    COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 0, 2, two, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CSSetUnorderedAccessViews), 2);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 1), 0);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 2), 2);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 4), 0);
        T_EQ(T_BACK_ELEMENT(CSSetUnorderedAccessViews, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(CSSetUnorderedAccessViews, 3, 1), T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 3, 0, NULL, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CSSetUnorderedAccessViews), 3);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 1), 3);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 2), 0);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 3), 0);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 4), 0);
        T_EQ(T_BACK_ARRAY_LEN(CSSetUnorderedAccessViews, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 7, 1, two, counts);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CSSetUnorderedAccessViews), 4);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 1), 7);
        T_EQ(T_BACK_ARG(CSSetUnorderedAccessViews, 2), 1);
        T_EQ(T_BACK_ELEMENT(CSSetUnorderedAccessViews, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(OMSetRenderTargetsAndUnorderedAccessViews), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11UnorderedAccessView const *wrong[1] = { (ID3D11UnorderedAccessView const *)T_BACK_NEW(device, &rt_nulldev_query_class) };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 6, 3, two, counts));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 0, 1, NULL, counts));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CSSetUnorderedAccessViews, context, 0, 1, wrong, counts));
}
