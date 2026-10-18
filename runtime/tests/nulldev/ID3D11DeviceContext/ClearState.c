/* Expected values.  None of them comes from the body.  The NULL of every pointer and the release of
 * each object a reset unbinds come from Microsoft's page of ID3D11DeviceContext::ClearState (read
 * 2026-10-10: "Restore all default settings ... to NULL"), with the Set methods' rule that a bound
 * interface is held: "The method will hold a reference to the interfaces passed in".  rt_nulldev_new
 * gives the caller one reference, so each object is back to one after the call.  The topology 0 is
 * UNDEFINED ("The primitive topology is set to UNDEFINED"); the value 0 is the notes' rule, from
 * memory.  The viewports set to NULL are the page's, read as the first viewport's bytes all zero.
 * The viewport count 0, the stencil reference 0, the sample mask 0xffffffff and the blend factor of
 * four 1.0f are the notes' rules: the page does not name them.  The device unchanged is the notes'
 * rule too. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, ClearState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);

    /* One object bound in every kind of slot the context has (rt/nulldev.h, NdContext): both constant
     * buffer arrays, shader-resource slot 127, sampler slot 15, render targets 0 and 7, the depth view,
     * the three shaders, the input layout and the three state slots.  hs and ds have no class to be made
     * of and stay NULL.  Each object is made by rt_nulldev_new and bound by rt_nulldev_bind, which holds
     * it: two references. */
    struct { void *slot; const RtComClass *cls; } binds[] = {
        { &c->vs_cb[3],       &rt_nulldev_buffer_class },
        { &c->vs_cb[13],      &rt_nulldev_buffer_class },
        { &c->ps_cb[0],       &rt_nulldev_buffer_class },
        { &c->ps_srv[127],    &rt_nulldev_srv_class },
        { &c->ps_sampler[15], &rt_nulldev_sampler_class },
        { &c->vs,             &rt_nulldev_vs_class },
        { &c->ps,             &rt_nulldev_ps_class },
        { &c->gs,             &rt_nulldev_gs_class },
        { &c->input_layout,   &rt_nulldev_inputlayout_class },
        { &c->rtv[0],         &rt_nulldev_rtv_class },
        { &c->rtv[7],         &rt_nulldev_rtv_class },
        { &c->dsv,            &rt_nulldev_dsv_class },
        { &c->blend,          &rt_nulldev_blend_class },
        { &c->depth_stencil,  &rt_nulldev_depthstencil_class },
        { &c->rasterizer,     &rt_nulldev_rasterizer_class },
    };
    const unsigned n = sizeof binds / sizeof binds[0];
    void *held[sizeof binds / sizeof binds[0]];
    for (unsigned i = 0; i < n; i++) {
        held[i] = rt_nulldev_new(device, binds[i].cls);
        rt_nulldev_bind(binds[i].slot, held[i]);
    }
    c->topology = 4;
    c->nviewports = 1;
    c->viewport[0] = (D3D11_VIEWPORT){ 1.0f, 2.0f, 640.0f, 480.0f, 0.0f, 1.0f };
    c->stencil_ref = 9;
    c->sample_mask = 5;
    for (UINT i = 0; i < 4; i++)
        c->blend_factor[i] = 0.25f;
    ID3D11Device *dev_before = c->child.device;

    T_CASE("a bind before the call holds one reference to each object");
    for (unsigned i = 0; i < n; i++) {
        T_TRUE(*(void **)binds[i].slot == held[i]);
        T_EQ(rt_com_refs(held[i]), 2);
    }

    COM_CALL(ID3D11DeviceContext, ClearState, context);

    T_CASE("ClearState nulls every pointer of the record");
    for (UINT i = 0; i < ND_CB_SLOTS; i++) {
        T_TRUE(c->vs_cb[i] == NULL);
        T_TRUE(c->ps_cb[i] == NULL);
    }
    for (UINT i = 0; i < ND_SRV_SLOTS; i++)
        T_TRUE(c->ps_srv[i] == NULL);
    for (UINT i = 0; i < ND_SAMPLER_SLOTS; i++)
        T_TRUE(c->ps_sampler[i] == NULL);
    for (UINT i = 0; i < ND_RT_SLOTS; i++)
        T_TRUE(c->rtv[i] == NULL);
    T_TRUE(c->vs == NULL);
    T_TRUE(c->ps == NULL);
    T_TRUE(c->gs == NULL);
    T_TRUE(c->hs == NULL);
    T_TRUE(c->ds == NULL);
    T_TRUE(c->input_layout == NULL);
    T_TRUE(c->dsv == NULL);
    T_TRUE(c->blend == NULL);
    T_TRUE(c->depth_stencil == NULL);
    T_TRUE(c->rasterizer == NULL);

    T_CASE("ClearState releases each object it unbinds");
    for (unsigned i = 0; i < n; i++)
        T_EQ(rt_com_refs(held[i]), 1);

    T_CASE("topology, viewport count and stencil reference go to 0");
    T_EQ(c->topology, 0);
    T_EQ(c->nviewports, 0);
    T_EQ(c->stencil_ref, 0);

    T_CASE("the first viewport is all zero bytes");
    unsigned char zero[sizeof c->viewport[0]];
    memset(zero, 0, sizeof zero);
    T_TRUE(!memcmp(&c->viewport[0], zero, sizeof zero));

    T_CASE("sample mask goes to 0xffffffff and the blend factor to four times 1.0f");
    T_EQ(c->sample_mask, 0xffffffffu);
    for (UINT i = 0; i < 4; i++)
        T_TRUE(c->blend_factor[i] == 1.0f);

    T_CASE("child.device is unchanged");
    T_TRUE(c->child.device == dev_before);
}
