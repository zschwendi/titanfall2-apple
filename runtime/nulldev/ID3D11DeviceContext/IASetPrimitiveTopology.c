/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetprimitivetopology
 * RT-CASES:  a Set stores the topology; the undefined topology 0 is stored; the adjacency topology 13 is stored
 * RT-TRAPS:  IASetPrimitiveTopology with topology
 */
#include "rt/nulldev.h"

/* The topology values 0 to 5 and 10 to 13 are the ones the notes accept (rt/win/d3d11.h,
 * D3D11_PRIMITIVE_TOPOLOGY_*); the patch lists and the other values are a stop.  The page gives
 * no list of values; the notes' rule is the source of the stop. */
COM_METHOD(ID3D11DeviceContext, void, IASetPrimitiveTopology, D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if ((Topology > 5 && Topology < 10) || Topology > 13)
        RT_UNIMPLEMENTED(ctx, "IASetPrimitiveTopology with topology %u", Topology);
    c->topology = Topology;
    RT_BACK(ctx, IASetPrimitiveTopology, c->child.back, Topology);
}
