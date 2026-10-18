/* Expected values.  The viewports are the test's own constants, and the Get method's answers are
 * compared with them; none of these values comes from the body.  Microsoft's page of
 * ID3D11DeviceContext::RSSetViewports (read 2026-10-10) says "Any viewports not
 * defined by the call are disabled", so a Set replaces the count.  The count limit 16 is
 * ND_VIEWPORTS, from the notes; the NULL-array stop is the notes' rule. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, RSSetViewports)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    D3D11_VIEWPORT v[2] = { { 1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 1.0f },
                            { 5.0f, 6.0f, 7.0f, 8.0f, 0.25f, 0.75f } };
    D3D11_VIEWPORT big[17];
    memset(big, 0, sizeof big);

    T_CASE("a Set stores the viewports and their count");
    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 2, v);
    T_EQ(c->nviewports, 2);
    T_TRUE(memcmp(c->viewport, v, sizeof v) == 0);

    T_CASE("a Set with fewer viewports replaces the count");
    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 1, v);
    T_EQ(c->nviewports, 1);
    T_TRUE(memcmp(c->viewport, v, sizeof v[0]) == 0);

    T_CASE("a Set with 0 viewports disables all");
    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 0, NULL);
    T_EQ(c->nviewports, 0);

    T_CASE("the largest count of 16 is accepted");
    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 16, big);
    T_EQ(c->nviewports, 16);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 17, big), "RSSetViewports with 17 viewports");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 1, NULL), "RSSetViewports with a NULL array");
}
