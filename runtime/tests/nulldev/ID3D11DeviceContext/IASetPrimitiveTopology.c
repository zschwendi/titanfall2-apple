/* Expected values.  The topology's default, undefined (0), is from the notes (Microsoft's page of
 * ID3D11DeviceContext::ClearState, from memory).  The accepted values 0 to 5 and 10 to 13 and the
 * stops for 6, 33 and the patch lists come from the notes; the enumerator values are from
 * rt/win/d3d11.h.  The stored value is read back through the context's record, which the body
 * does not compute.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, IASetPrimitiveTopology)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);

    T_CASE("a Set stores the topology");
    T_TRUE(c->topology == 0);
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    T_TRUE(c->topology == 4);

    T_CASE("the undefined topology 0 is stored");
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    T_TRUE(c->topology == 0);

    T_CASE("the adjacency topology 13 is stored");
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ);
    T_TRUE(c->topology == 13);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 6), "IASetPrimitiveTopology with topology 6");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 33), "IASetPrimitiveTopology with topology 33");
    T_TRUE(c->topology == 13);
}
