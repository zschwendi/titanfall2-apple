/* Expected values.  The reference counts and the NULL reads come from Microsoft's page of
 * ID3D11DeviceContext::IAGetInputLayout (read 2026-10-10: "Any returned interfaces will have their
 * reference count incremented by one") and of ID3D11DeviceContext::IASetInputLayout ("The method
 * will hold a reference to the interfaces passed in"): rt_nulldev_new gives the caller one
 * reference, a bound layout holds one more, and each Get hands out one more, which this test does
 * not release.  The bindings are made with IASetInputLayout, the only way to bind.  The NULL
 * out-pointer stop and a Get after a NULL Set reading NULL are the notes' rules.  None of these
 * values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, IAGetInputLayout)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11InputLayout *a = rt_nulldev_new(device, &rt_nulldev_inputlayout_class);
    ID3D11InputLayout *b = rt_nulldev_new(device, &rt_nulldev_inputlayout_class);
    ID3D11InputLayout *got;

    T_CASE("a slot never set reads NULL");
    got = a;
    COM_CALL(ID3D11DeviceContext, IAGetInputLayout, context, &got);
    T_TRUE(got == NULL);
    T_TRUE(c->input_layout == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Get returns the bound layout and adds one reference");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, a);
    got = NULL;
    COM_CALL(ID3D11DeviceContext, IAGetInputLayout, context, &got);
    T_TRUE(got == a);
    T_EQ(rt_com_refs(a), 3);

    T_CASE("each Get hands out one more reference");
    got = NULL;
    COM_CALL(ID3D11DeviceContext, IAGetInputLayout, context, &got);
    T_TRUE(got == a);
    T_EQ(rt_com_refs(a), 4);

    T_CASE("a NULL unbinds, and a Get then reads NULL and adds nothing");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, NULL);
    T_EQ(rt_com_refs(a), 3);
    got = a;
    COM_CALL(ID3D11DeviceContext, IAGetInputLayout, context, &got);
    T_TRUE(got == NULL);
    T_EQ(rt_com_refs(a), 3);

    T_CASE("a Get after another Set returns the new layout");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, b);
    got = NULL;
    COM_CALL(ID3D11DeviceContext, IAGetInputLayout, context, &got);
    T_TRUE(got == b);
    T_EQ(rt_com_refs(a), 3);
    T_EQ(rt_com_refs(b), 3);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, IAGetInputLayout, context, NULL), "IAGetInputLayout with a NULL out-pointer");
}
