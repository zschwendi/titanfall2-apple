/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::OMGetBlendState (read 2026-10-10: "The reference count of the returned
 * interface will be incremented by one when the blend state is retrieved"): each Get hands the
 * caller one more reference, and the test releases it with rt_com_release, as a game would.  The
 * new-context values NULL, {1, 1, 1, 1} and 0xffffffff are the notes' defaults; the same two values
 * are on OMSetBlendState's page ("If you pass NULL ... { 1, 1, 1, 1 }" and "The default value is
 * 0xffffffff").  The factor and mask set here are test inputs, exact in binary floating point.  None
 * of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, OMGetBlendState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11BlendState *a = rt_nulldev_new(device, &rt_nulldev_blend_class);
    ID3D11BlendState *b = rt_nulldev_new(device, &rt_nulldev_blend_class);
    const FLOAT factor[4] = { 0.25f, 0.5f, 0.75f, 0.125f };
    ID3D11BlendState *p;
    FLOAT f[4];
    UINT m;

    T_CASE("a new context reads NULL, {1, 1, 1, 1} and 0xffffffff");
    p = b;
    f[0] = f[1] = f[2] = f[3] = 0.0f;
    m = 0;
    COM_CALL(ID3D11DeviceContext, OMGetBlendState, context, &p, f, &m);
    T_TRUE(p == NULL);
    T_TRUE(f[0] == 1.0f);
    T_TRUE(f[1] == 1.0f);
    T_TRUE(f[2] == 1.0f);
    T_TRUE(f[3] == 1.0f);
    T_TRUE(m == 0xffffffffu);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a Get returns the bound state with one more reference");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, a, factor, 0x5u);
    T_EQ(rt_com_refs(a), 2);
    p = NULL;
    COM_CALL(ID3D11DeviceContext, OMGetBlendState, context, &p, f, &m);
    T_TRUE(p == a);
    T_EQ(rt_com_refs(a), 3);
    rt_com_release(p);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get returns the stored blend factor and sample mask");
    p = NULL;
    f[0] = f[1] = f[2] = f[3] = 9.0f;
    m = 0x77u;
    COM_CALL(ID3D11DeviceContext, OMGetBlendState, context, &p, f, &m);
    T_TRUE(f[0] == 0.25f);
    T_TRUE(f[1] == 0.5f);
    T_TRUE(f[2] == 0.75f);
    T_TRUE(f[3] == 0.125f);
    T_TRUE(m == 0x5u);
    rt_com_release(p);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a NULL out-pointer is skipped and the others are still written");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, b, NULL, 0x9u);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);
    f[0] = f[1] = f[2] = f[3] = 9.0f;
    m = 0x77u;
    COM_CALL(ID3D11DeviceContext, OMGetBlendState, context, NULL, f, &m);
    T_TRUE(f[0] == 1.0f);
    T_TRUE(f[1] == 1.0f);
    T_TRUE(f[2] == 1.0f);
    T_TRUE(f[3] == 1.0f);
    T_TRUE(m == 0x9u);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);
    p = NULL;
    COM_CALL(ID3D11DeviceContext, OMGetBlendState, context, &p, NULL, NULL);
    T_TRUE(p == b);
    T_EQ(rt_com_refs(b), 3);
    rt_com_release(p);
    T_EQ(rt_com_refs(b), 2);
    f[0] = f[1] = f[2] = f[3] = 9.0f;
    m = 0x77u;
    COM_CALL(ID3D11DeviceContext, OMGetBlendState, context, NULL, NULL, NULL);
    T_TRUE(f[0] == 9.0f);
    T_TRUE(f[1] == 9.0f);
    T_TRUE(f[2] == 9.0f);
    T_TRUE(f[3] == 9.0f);
    T_TRUE(m == 0x77u);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);
}
