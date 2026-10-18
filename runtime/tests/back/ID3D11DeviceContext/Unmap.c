/* The forward of ID3D11DeviceContext::Unmap through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and the resource cross as
 * their back objects, the subresource as the game passed it.  Rule 5: with a back end the mapping
 * is the back end's, and the front's record of it (NdMemory.back_mapped and the count of mapped
 * subresources) is what Unmap ends; with `none` the same count ends.  Rule 1: each stop of the body
 * comes before the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, Unmap)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC d = { .ByteWidth = 64, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_VERTEX_BUFFER,
                            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, NULL, &buf), S_OK);
    T_REQUIRE(buf != NULL);
    D3D11_MAPPED_SUBRESOURCE m = { 0 };

    T_CASE("a mapped buffer unmaps: no subresource is mapped any more, and the back end is handed the unmap");
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);
    T_EQ(rt_nulldev_memory(buf)->mapped, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Unmap), 1);
        T_EQ(T_BACK_ARG(Unmap, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(Unmap, 1), T_BACK_OF(buf));
        T_EQ(T_BACK_ARG(Unmap, 2), 0);
    }

    T_CASE("after Unmap the buffer maps again, and unmaps again");
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    T_EQ(rt_nulldev_memory(buf)->mapped, 1);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);
    T_EQ(rt_nulldev_memory(buf)->mapped, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Unmap), 2);
    }

    T_CASE("the stops, each before the back end: an Unmap of a NULL resource, of a subresource the buffer does not "
           "have, and of one that is not mapped");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Unmap, context, NULL, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 1));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0));
    ID3D11Resource *ns = (ID3D11Resource *)T_BACK_NEW(device, &rt_nulldev_sampler_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Unmap, context, ns, 0));
}
