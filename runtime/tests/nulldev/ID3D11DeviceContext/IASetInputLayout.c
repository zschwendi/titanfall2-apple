/* Expected values.  The reference counts and the NULL reads come from Microsoft's page of
 * ID3D11DeviceContext::IASetInputLayout (read 2026-10-10: "The method will hold a reference to
 * the interfaces passed in"): rt_nulldev_new gives the caller one reference, and the bound layout
 * holds one more.  The NULL unbinding and the NULL over an empty slot doing nothing are the
 * notes' rules.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, IASetInputLayout)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11InputLayout *a = rt_nulldev_new(device, &rt_nulldev_inputlayout_class);
    ID3D11InputLayout *b = rt_nulldev_new(device, &rt_nulldev_inputlayout_class);

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->input_layout == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the layout and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, a);
    T_TRUE(c->input_layout == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over a bound layout releases the old one");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, b);
    T_TRUE(c->input_layout == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL unbinds the slot and releases the layout");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, NULL);
    T_TRUE(c->input_layout == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a NULL over an empty slot does nothing");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, NULL);
    T_TRUE(c->input_layout == NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 1);
}
