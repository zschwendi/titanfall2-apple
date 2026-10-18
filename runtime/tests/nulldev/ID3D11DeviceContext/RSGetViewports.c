/* Expected values.  The viewports are the test's own constants, set through the Set method.
 * Microsoft's page of ID3D11DeviceContext::RSGetViewports (read 2026-10-10) says
 * that pNumViewports is in and out, that the unused elements of the array contain 0, and that the
 * count returned is the number of viewports set; a NULL array returns only the count.  The limit
 * 16 is ND_VIEWPORTS, from the notes; the stops are the notes' rules.  Bytes past the caller's
 * room are checked against a sentinel pattern, so nothing written past the third element is missed.
 * None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, RSGetViewports)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_VIEWPORT v[2] = { { 1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 1.0f },
                            { 5.0f, 6.0f, 7.0f, 8.0f, 0.25f, 0.75f } };
    D3D11_VIEWPORT out[4], zero, big[17];
    memset(&zero, 0, sizeof zero);
    memset(big, 0, sizeof big);
    UINT n;

    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 2, v);

    T_CASE("a Get with a NULL array returns the count");
    n = 99;
    COM_CALL(ID3D11DeviceContext, RSGetViewports, context, &n, NULL);
    T_EQ(n, 2);

    T_CASE("a Get with room fills the viewports, zeroes the rest and returns the count");
    memset(out, 0xAB, sizeof out);
    n = 3;
    COM_CALL(ID3D11DeviceContext, RSGetViewports, context, &n, out);
    T_EQ(n, 2);
    T_TRUE(memcmp(&out[0], &v[0], sizeof v[0]) == 0);
    T_TRUE(memcmp(&out[1], &v[1], sizeof v[1]) == 0);
    T_TRUE(memcmp(&out[2], &zero, sizeof zero) == 0);
    for (size_t i = 0; i < sizeof out[3]; i++)
        T_TRUE(((unsigned char *)&out[3])[i] == 0xAB);

    T_CASE("a Get with room for exactly the count returns them all");
    memset(out, 0xAB, sizeof out);
    n = 2;
    COM_CALL(ID3D11DeviceContext, RSGetViewports, context, &n, out);
    T_EQ(n, 2);
    T_TRUE(memcmp(&out[0], &v[0], sizeof v[0]) == 0);
    T_TRUE(memcmp(&out[1], &v[1], sizeof v[1]) == 0);
    T_TRUE(((unsigned char *)&out[2])[0] == 0xAB);

    T_CASE("a Get on a new context returns 0");
    ID3D11Device *device2;
    ID3D11DeviceContext *context2;
    rt_nulldev_create(&device2, &context2);
    n = 99;
    COM_CALL(ID3D11DeviceContext, RSGetViewports, context2, &n, NULL);
    T_EQ(n, 0);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, RSGetViewports, context, NULL, out), "RSGetViewports with a NULL count");
    n = 17;
    T_TRAPS(COM_CALL(ID3D11DeviceContext, RSGetViewports, context, &n, big), "RSGetViewports for 17 viewports");
    n = 1;
    T_TRAPS(COM_CALL(ID3D11DeviceContext, RSGetViewports, context, &n, out), "RSGetViewports with room for 1 of 2 viewports");
}
