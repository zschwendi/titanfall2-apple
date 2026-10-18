/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::RSGetState (read 2026-10-10: "Any returned interfaces will have their
 * reference count incremented by one"): the bound state has one reference from the Set, and
 * each Get hands the caller one more.  The NULL read with nothing bound is the notes' rule; the
 * NULL out-pointer stop is the notes' rule too.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, RSGetState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11RasterizerState *a = rt_nulldev_new(device, &rt_nulldev_rasterizer_class);
    ID3D11RasterizerState *got = NULL;

    T_CASE("a Get with nothing bound returns NULL");
    COM_CALL(ID3D11DeviceContext, RSGetState, context, &got);
    T_TRUE(got == NULL);
    T_EQ(rt_com_refs(a), 1);

    COM_CALL(ID3D11DeviceContext, RSSetState, context, a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get returns the bound rasterizer state with one more reference");
    got = NULL;
    COM_CALL(ID3D11DeviceContext, RSGetState, context, &got);
    T_TRUE(got == a);
    T_EQ(rt_com_refs(a), 3);
    COM_CALL(ID3D11DeviceContext, RSSetState, context, NULL);
    T_EQ(rt_com_refs(a), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, RSGetState, context, NULL), "RSGetState with a NULL out-pointer");
}
