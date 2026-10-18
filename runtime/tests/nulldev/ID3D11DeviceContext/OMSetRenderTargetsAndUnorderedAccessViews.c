/* Expected values.  The reference counts come from Microsoft's pages of the Set methods of
 * Direct3D 11 ("The method will hold a reference to the interfaces passed in") and from
 * rt_nulldev_new, which gives the caller one reference.  The remarks of
 * ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews (read 10 October by unit
 * d3d-rows-fix; the page as read 2026-10-10): NumRTVs and NumUAVs keep values, the stop conditions
 * (NumRTVs <= 8, UAVStartSlot >= NumRTVs, UAVStartSlot + NumUAVs <= 8), the unbinding of shader
 * resource views that conflict with the bound targets and UAVs, and that a keep value for NumRTVs
 * ignores the depth-stencil view.  The stops 5 and 6 are the notes' rules where the page's two
 * sentences differ.  The views are attached to textures by hand: a view stands for the whole
 * texture.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

static void *attach(void *view, const RtComClass *cls, void *texture)
{
    NdView *v = rt_com_state_of(view, cls);
    rt_com_addref(texture);
    v->resource = (ID3D11Resource *)texture;
    return view;
}

TEST(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    void *tA = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    void *tB = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    void *tD = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    void *tU = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    void *tR = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    void *tO = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    ID3D11RenderTargetView *rtA = attach(rt_nulldev_new(device, &rt_nulldev_rtv_class), &rt_nulldev_rtv_class, tA);
    ID3D11RenderTargetView *rtB = attach(rt_nulldev_new(device, &rt_nulldev_rtv_class), &rt_nulldev_rtv_class, tB);
    ID3D11RenderTargetView *rtR = attach(rt_nulldev_new(device, &rt_nulldev_rtv_class), &rt_nulldev_rtv_class, tR);
    ID3D11DepthStencilView *dsv = attach(rt_nulldev_new(device, &rt_nulldev_dsv_class), &rt_nulldev_dsv_class, tD);
    ID3D11UnorderedAccessView *uav0 = rt_nulldev_new(device, &rt_nulldev_uav_class);
    ID3D11UnorderedAccessView *uavU = attach(rt_nulldev_new(device, &rt_nulldev_uav_class), &rt_nulldev_uav_class, tU);
    ID3D11UnorderedAccessView *uavA = attach(rt_nulldev_new(device, &rt_nulldev_uav_class), &rt_nulldev_uav_class, tA);
    ID3D11ShaderResourceView *srvU = attach(rt_nulldev_new(device, &rt_nulldev_srv_class), &rt_nulldev_srv_class, tU);
    ID3D11ShaderResourceView *srvR = attach(rt_nulldev_new(device, &rt_nulldev_srv_class), &rt_nulldev_srv_class, tR);
    ID3D11ShaderResourceView *srvO = attach(rt_nulldev_new(device, &rt_nulldev_srv_class), &rt_nulldev_srv_class, tO);
    ID3D11Buffer *buf = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11RenderTargetView const *two[2] = { rtA, rtB }, *one[1] = { rtA }, *one_r[1] = { rtR };
    ID3D11RenderTargetView const *nine[9] = { NULL };
    ID3D11UnorderedAccessView const *u0[1] = { uav0 }, *uU[1] = { uavU }, *uA[1] = { uavA };
    ID3D11UnorderedAccessView const *ubuf[1] = { (ID3D11UnorderedAccessView const *)buf };

    T_CASE("two views and a depth view bound with UAVStartSlot 2 and no UAVs");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 2, two, dsv, 2, 0, NULL, NULL);
    T_TRUE(c->rtv[0] == rtA);
    T_TRUE(c->rtv[1] == rtB);
    T_TRUE(c->rtv[2] == NULL);
    T_TRUE(c->rtv[7] == NULL);
    T_TRUE(c->dsv == dsv);
    T_EQ(rt_com_refs(rtA), 2);
    T_EQ(rt_com_refs(rtB), 2);
    T_EQ(rt_com_refs(dsv), 2);

    T_CASE("keep value with one UAV leaves render targets and depth-stencil view bound");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, NULL, NULL, 2, 1, u0, NULL);
    T_TRUE(c->rtv[0] == rtA);
    T_TRUE(c->rtv[1] == rtB);
    T_TRUE(c->rtv[2] == NULL);
    T_TRUE(c->dsv == dsv);
    T_EQ(rt_com_refs(rtA), 2);
    T_EQ(rt_com_refs(rtB), 2);
    T_EQ(rt_com_refs(dsv), 2);
    T_EQ(rt_com_refs(uav0), 1);

    T_CASE("with the UAV keep value one view is recorded and slot 1 released, whatever UAVStartSlot is");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, one, dsv, 5, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, NULL, NULL);
    T_TRUE(c->rtv[0] == rtA);
    T_TRUE(c->rtv[1] == NULL);
    T_TRUE(c->dsv == dsv);
    T_EQ(rt_com_refs(rtA), 2);
    T_EQ(rt_com_refs(rtB), 1);
    T_EQ(rt_com_refs(dsv), 2);

    T_CASE("a shader-resource view of a UAV's texture is unbound");
    rt_nulldev_bind(&c->ps_srv[0], srvU);
    rt_nulldev_bind(&c->ps_srv[1], srvR);
    rt_nulldev_bind(&c->ps_srv[2], srvO);
    T_EQ(rt_com_refs(srvU), 2);
    T_EQ(rt_com_refs(srvR), 2);
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, one_r, dsv, 1, 1, uU, NULL);
    T_TRUE(c->ps_srv[0] == NULL);
    T_EQ(rt_com_refs(srvU), 1);
    T_EQ(rt_com_refs(uavU), 1);

    T_CASE("a shader-resource view of a render-target's texture is unbound while a view of another texture stays");
    T_TRUE(c->ps_srv[1] == NULL);
    T_EQ(rt_com_refs(srvR), 1);
    T_TRUE(c->ps_srv[2] == srvO);
    T_EQ(rt_com_refs(srvO), 2);
    T_EQ(rt_com_refs(rtA), 1);
    T_EQ(rt_com_refs(rtR), 2);
    T_TRUE(c->rtv[0] == rtR);
    T_TRUE(c->dsv == dsv);

    /* Setup for the traps: two render targets bound in slots 0 and 1 (same call as the first case). */
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 2, two, dsv, 2, 0, NULL, NULL);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 9, nine, dsv, 0, 0, NULL, NULL), "OMSetRenderTargetsAndUnorderedAccessViews with 9 views");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 0, NULL, NULL, 7, 2, NULL, NULL), "OMSetRenderTargetsAndUnorderedAccessViews with unordered-access views in slots 7 to 8");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 3, nine, dsv, 2, 1, uU, NULL), "OMSetRenderTargetsAndUnorderedAccessViews with UAVs from slot 2 and 3 render targets");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, NULL, dsv, 1, 0, NULL, NULL), "OMSetRenderTargetsAndUnorderedAccessViews with a NULL array");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, NULL, NULL, 1, 0, NULL, NULL), "OMSetRenderTargetsAndUnorderedAccessViews keeping render targets with one bound in slot 1");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, NULL, NULL, 2, 1, uA, NULL), "OMSetRenderTargetsAndUnorderedAccessViews with an unordered-access view of a kept render target");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 0, NULL, NULL, 0, 1, ubuf, NULL), "is an object of class");
}
