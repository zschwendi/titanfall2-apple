/* Expected values.  The reference a Get hands out comes from Microsoft's page of
 * ID3D11DeviceContext::OMGetDepthStencilState (read 2026-10-10: "Any returned interfaces will have
 * their reference count incremented by one").  The reference the bound state holds comes from the
 * page of ID3D11DeviceContext::OMSetDepthStencilState ("The method will hold a reference to the
 * interfaces passed in").  So rt_nulldev_new gives the caller one reference, the Set adds one, and
 * each Get that returns the state adds one more.  A new context reads NULL and stencil reference 0,
 * as the notes give them.  The stencil reference 0x42 and 0x99 are values the test passes to the
 * Set.  The sentinels (the out-pointers' old values and 0xdeadbeef) show that a Get writes its
 * out-pointers.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, OMGetDepthStencilState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11DepthStencilState *a = rt_nulldev_new(device, &rt_nulldev_depthstencil_class);
    ID3D11DepthStencilState *b = rt_nulldev_new(device, &rt_nulldev_depthstencil_class);
    ID3D11DepthStencilState *got = b;
    UINT ref = 0xdeadbeefu;

    T_CASE("a new context reads NULL and stencil reference 0");
    T_TRUE(c->depth_stencil == NULL);
    COM_CALL(ID3D11DeviceContext, OMGetDepthStencilState, context, &got, &ref);
    T_TRUE(got == NULL);
    T_EQ(ref, 0);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a Get returns the bound state with one more reference");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, a, 0x42);
    T_EQ(rt_com_refs(a), 2);
    got = b;
    ref = 0xdeadbeefu;
    COM_CALL(ID3D11DeviceContext, OMGetDepthStencilState, context, &got, &ref);
    T_TRUE(got == a);
    T_EQ(ref, 0x42);
    T_EQ(rt_com_refs(a), 3);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a NULL out-pointer is skipped");
    ref = 0xdeadbeefu;
    COM_CALL(ID3D11DeviceContext, OMGetDepthStencilState, context, NULL, &ref);
    T_EQ(ref, 0x42);
    T_EQ(rt_com_refs(a), 3);
    got = NULL;
    COM_CALL(ID3D11DeviceContext, OMGetDepthStencilState, context, &got, NULL);
    T_TRUE(got == a);
    T_EQ(rt_com_refs(a), 4);

    T_CASE("a Get after a NULL Set reads NULL");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, NULL, 0x99);
    T_TRUE(c->depth_stencil == NULL);
    T_EQ(rt_com_refs(a), 3);
    got = a;
    ref = 0xdeadbeefu;
    COM_CALL(ID3D11DeviceContext, OMGetDepthStencilState, context, &got, &ref);
    T_TRUE(got == NULL);
    T_EQ(ref, 0x99);
    T_EQ(rt_com_refs(a), 3);
}
