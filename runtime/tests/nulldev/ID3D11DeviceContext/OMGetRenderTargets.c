/* Expected values.  "Any returned interfaces will have their reference count incremented by one"
 * (Microsoft's page of ID3D11DeviceContext::OMGetRenderTargets, read 2026-10-10) gives each count
 * here: the caller of Get holds one more reference to each view it is handed.  The views are bound
 * with COM_CALL to OMSetRenderTargets, whose own test is separate.  A NULL pointer means that part
 * is not wanted, and an array slot that is not written keeps its marker.  The 9-view stop is the
 * notes' rule.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, OMGetRenderTargets)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11RenderTargetView *r1 = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    ID3D11RenderTargetView *r2 = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    ID3D11DepthStencilView *d1 = rt_nulldev_new(device, &rt_nulldev_dsv_class);
    ID3D11RenderTargetView const *two[2] = { r1, r2 };
    ID3D11RenderTargetView *out[8];
    ID3D11DepthStencilView *dout;
    ID3D11RenderTargetView const *nine[9] = { NULL };
    int sentinel = 0;   /* a marker in an array slot the Get must not write: compared, never used */
    ID3D11RenderTargetView *mark = (ID3D11RenderTargetView *)(void *)&sentinel;

    T_CASE("a Get of a slot never set returns NULL");
    out[0] = mark;
    out[1] = mark;
    COM_CALL(ID3D11DeviceContext, OMGetRenderTargets, context, 1, out, NULL);
    T_TRUE(out[0] == NULL);
    T_EQ(rt_com_refs(r1), 1);

    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 2, two, d1);
    T_EQ(rt_com_refs(r1), 2);
    T_EQ(rt_com_refs(r2), 2);
    T_EQ(rt_com_refs(d1), 2);

    T_CASE("a Get returns the bound view and adds one reference");
    out[0] = NULL;
    out[1] = NULL;
    out[2] = mark;
    COM_CALL(ID3D11DeviceContext, OMGetRenderTargets, context, 2, out, NULL);
    T_TRUE(out[0] == r1);
    T_TRUE(out[1] == r2);
    T_TRUE(out[2] == mark);
    T_EQ(rt_com_refs(r1), 3);
    T_EQ(rt_com_refs(r2), 3);
    T_EQ(rt_com_refs(d1), 2);
    rt_com_release(r1);
    rt_com_release(r2);
    T_EQ(rt_com_refs(r1), 2);

    T_CASE("a Get of the depth-stencil view returns it and adds one reference");
    dout = NULL;
    COM_CALL(ID3D11DeviceContext, OMGetRenderTargets, context, 0, NULL, &dout);
    T_TRUE(dout == d1);
    T_EQ(rt_com_refs(d1), 3);
    rt_com_release(d1);
    T_EQ(rt_com_refs(d1), 2);

    T_CASE("a Get with NULL pointers changes no count");
    COM_CALL(ID3D11DeviceContext, OMGetRenderTargets, context, 8, NULL, NULL);
    T_EQ(rt_com_refs(r1), 2);
    T_EQ(rt_com_refs(r2), 2);
    T_EQ(rt_com_refs(d1), 2);

    T_CASE("a Get of fewer than 8 views leaves the rest of the array untouched");
    out[0] = NULL;
    out[1] = NULL;
    out[2] = mark;
    COM_CALL(ID3D11DeviceContext, OMGetRenderTargets, context, 2, out, NULL);
    T_TRUE(out[2] == mark);
    rt_com_release(r1);
    rt_com_release(r2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMGetRenderTargets, context, 9, (ID3D11RenderTargetView **)nine, NULL), "OMGetRenderTargets with");
}
