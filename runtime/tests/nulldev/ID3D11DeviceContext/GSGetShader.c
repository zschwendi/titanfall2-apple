/* Expected values.  The reference the caller gets for a returned shader comes from Microsoft's
 * page of ID3D11DeviceContext::GSGetShader (read 2026-10-10: "Any returned interfaces will have
 * their reference count incremented by one"): rt_nulldev_new gives the caller one reference, the
 * shader bound to the slot holds one more, and each Get hands out one more that this test never
 * releases, so the counts below rise by one per Get.  The bound shader is set with the game's own
 * call, GSSetShader.  The count written as 0, the array left alone, a NULL count allowed and the
 * NULL out-pointer stop are the notes' rules.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, GSGetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11GeometryShader *a = rt_nulldev_new(device, &rt_nulldev_gs_class);
    ID3D11GeometryShader *b = rt_nulldev_new(device, &rt_nulldev_gs_class);
    ID3D11GeometryShader *out;
    UINT n;
    char marker = 0;
    ID3D11ClassInstance *inst[1] = { (ID3D11ClassInstance *)&marker };

    T_CASE("a slot never set reads NULL");
    out = b;
    n = 7;
    COM_CALL(ID3D11DeviceContext, GSGetShader, context, &out, NULL, &n);
    T_TRUE(out == NULL);
    T_EQ(n, 0);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Get hands out the bound shader with one more reference");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, a, NULL, 0);
    out = NULL;
    n = 7;
    COM_CALL(ID3D11DeviceContext, GSGetShader, context, &out, NULL, &n);
    T_TRUE(out == a);
    T_EQ(n, 0);
    T_EQ(rt_com_refs(a), 3);

    T_CASE("a Get writes 0 through the count and leaves the class-instance array alone");
    out = NULL;
    n = 7;
    COM_CALL(ID3D11DeviceContext, GSGetShader, context, &out, inst, &n);
    T_TRUE(out == a);
    T_EQ(n, 0);
    T_TRUE(inst[0] == (ID3D11ClassInstance *)&marker);
    T_EQ(rt_com_refs(a), 4);

    T_CASE("a NULL count pointer is allowed");
    out = NULL;
    COM_CALL(ID3D11DeviceContext, GSGetShader, context, &out, NULL, NULL);
    T_TRUE(out == a);
    T_EQ(rt_com_refs(a), 5);

    T_CASE("a Get after a Set of another shader hands out that shader");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, b, NULL, 0);
    out = NULL;
    COM_CALL(ID3D11DeviceContext, GSGetShader, context, &out, NULL, NULL);
    T_TRUE(out == b);
    T_EQ(rt_com_refs(a), 4);
    T_EQ(rt_com_refs(b), 3);

    T_CASE("a Get after a Set of NULL hands out NULL");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, NULL, NULL, 0);
    out = b;
    COM_CALL(ID3D11DeviceContext, GSGetShader, context, &out, NULL, NULL);
    T_TRUE(out == NULL);
    T_EQ(rt_com_refs(b), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, GSGetShader, context, NULL, NULL, NULL), "GSGetShader with a NULL out-pointer");
}
