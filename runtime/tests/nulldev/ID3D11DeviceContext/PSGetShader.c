/* Expected values.  The returned reference comes from Microsoft's page of
 * ID3D11DeviceContext::PSGetShader (read 2026-10-10: "Any returned interfaces will have their
 * reference count incremented by one"); the bound shader holds one reference, rt_nulldev_new
 * gives the caller one.  The 0 instances, the untouched instance array and the NULL out-pointer
 * stop are the notes' rules.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSGetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11PixelShader *a = rt_nulldev_new(device, &rt_nulldev_ps_class);
    ID3D11PixelShader *got;
    UINT n;
    int mark;
    ID3D11ClassInstance *insts[1] = { (ID3D11ClassInstance *)&mark };

    T_CASE("a Get of an unset shader returns NULL and writes 0 instances");
    got = a;
    n = 99;
    COM_CALL(ID3D11DeviceContext, PSGetShader, context, &got, insts, &n);
    T_TRUE(got == NULL);
    T_EQ(n, 0);
    T_TRUE(insts[0] == (ID3D11ClassInstance *)&mark);
    T_EQ(rt_com_refs(a), 1);

    COM_CALL(ID3D11DeviceContext, PSSetShader, context, a, NULL, 0);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get returns the bound shader with one more reference");
    got = NULL;
    n = 99;
    COM_CALL(ID3D11DeviceContext, PSGetShader, context, &got, insts, &n);
    T_TRUE(got == a);
    T_EQ(n, 0);
    T_TRUE(insts[0] == (ID3D11ClassInstance *)&mark);
    T_TRUE(c->ps == a);
    T_EQ(rt_com_refs(a), 3);

    T_CASE("a NULL instance count is allowed and writes nothing");
    got = NULL;
    n = 99;
    COM_CALL(ID3D11DeviceContext, PSGetShader, context, &got, insts, NULL);
    T_TRUE(got == a);
    T_EQ(n, 99);
    T_TRUE(insts[0] == (ID3D11ClassInstance *)&mark);
    T_EQ(rt_com_refs(a), 4);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetShader, context, NULL, insts, &n), "PSGetShader with a NULL out-pointer");
}
