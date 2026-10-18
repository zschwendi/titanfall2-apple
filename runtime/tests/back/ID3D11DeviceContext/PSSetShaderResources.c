/* The forward of ID3D11DeviceContext::PSSetShaderResources through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each view cross as
 * their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's own), a
 * NULL array as NULL; StartSlot and NumViews as passed.  Rule 1: each of the body's stops comes
 * before the back end is reached.  A view of a texture bound as a render target is recorded by
 * the front as NULL in its slot (the page's overlap rule; runtime/tests/nulldev/ID3D11DeviceContext/
 * PSSetShaderResources.c has its values and sources) and still crosses as its back object
 * (rule 2).  What the front records is the same with and without a back end. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, PSSetShaderResources)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11ShaderResourceView *a = T_BACK_NEW(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView *b = T_BACK_NEW(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView const *three[3] = { a, NULL, b };

    T_CASE("three slots: each view crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 4, 3, three);
    T_TRUE(c->ps_srv[4] == a && c->ps_srv[5] == NULL && c->ps_srv[6] == b);
    T_EQ(rt_com_refs(a), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetShaderResources), 1);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(PSSetShaderResources, 1), 4);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 2), 3);
        T_TRUE(T_BACK_ARG(PSSetShaderResources, 3) != 0 && T_BACK_ARG(PSSetShaderResources, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(PSSetShaderResources, 3), 3);
        T_EQ(T_BACK_ELEMENT(PSSetShaderResources, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(PSSetShaderResources, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(PSSetShaderResources, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 2, 0, NULL);
    T_TRUE(c->ps_srv[4] == a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetShaderResources), 2);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 1), 2);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 2), 0);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(PSSetShaderResources, 3), 0);
    }

    T_CASE("the last slot");
    ID3D11ShaderResourceView const *one[1] = { b };
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 127, 1, one);
    T_TRUE(c->ps_srv[127] == b);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetShaderResources), 3);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 1), 127);
        T_EQ(T_BACK_ELEMENT(PSSetShaderResources, 3, 0), T_BACK_OF(b));
        T_EQ(T_BACK_COUNT(VSSetShaderResources), 0);
    }

    T_CASE("a view of a texture bound as a render target crosses as its back object, though the slot holds NULL");
    ID3D11Texture2D *tex = T_BACK_NEW(device, &rt_nulldev_texture2d_class);
    ID3D11RenderTargetView *rtv = T_BACK_NEW(device, &rt_nulldev_rtv_class);
    ID3D11ShaderResourceView *sv = T_BACK_NEW(device, &rt_nulldev_srv_class);
    NdView *v;
    v = rt_com_state_of(sv, &rt_nulldev_srv_class);
    rt_com_addref(tex);
    v->resource = (ID3D11Resource *)tex;
    v = rt_com_state_of(rtv, &rt_nulldev_rtv_class);
    rt_com_addref(tex);
    v->resource = (ID3D11Resource *)tex;
    rt_nulldev_bind(&c->rtv[0], rtv);
    T_EQ(rt_com_refs(rtv), 2);
    one[0] = sv;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 9, 1, one);
    T_TRUE(c->ps_srv[9] == NULL);
    T_EQ(rt_com_refs(sv), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetShaderResources), 4);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 1), 9);
        T_EQ(T_BACK_ARG(PSSetShaderResources, 2), 1);
        T_EQ(T_BACK_ELEMENT(PSSetShaderResources, 3, 0), T_BACK_OF(sv));
        T_TRUE(T_BACK_OF(sv) != 0);
    }
    rt_nulldev_bind(&c->rtv[0], NULL);

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11ShaderResourceView const *nine[9] = { NULL };
    ID3D11ShaderResourceView const *wrong[1] = { (ID3D11ShaderResourceView const *)T_BACK_NEW(device, &rt_nulldev_query_class) };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 120, 9, nine));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 0, 1, wrong));
}
