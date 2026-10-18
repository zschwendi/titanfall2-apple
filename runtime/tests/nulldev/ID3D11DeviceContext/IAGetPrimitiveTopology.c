/* Expected values.  The topology reads 0 (undefined) on a new context: the notes, from Microsoft's
 * page of ID3D11DeviceContext::ClearState (from memory).  A Get after a Set returns what the Set
 * stored: 4 and 13 are the values set here.  The NULL out-pointer stop is the notes' rule.  The
 * enumerator values are from rt/win/d3d11.h.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, IAGetPrimitiveTopology)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_PRIMITIVE_TOPOLOGY t = 99;

    T_CASE("a Get before any Set reads the undefined topology 0");
    COM_CALL(ID3D11DeviceContext, IAGetPrimitiveTopology, context, &t);
    T_EQ(t, 0);

    T_CASE("a Get returns the topology last set");
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    t = 99;
    COM_CALL(ID3D11DeviceContext, IAGetPrimitiveTopology, context, &t);
    T_EQ(t, 4);
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ);
    t = 99;
    COM_CALL(ID3D11DeviceContext, IAGetPrimitiveTopology, context, &t);
    T_EQ(t, 13);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, IAGetPrimitiveTopology, context, NULL), "IAGetPrimitiveTopology with a NULL out-pointer");
}
