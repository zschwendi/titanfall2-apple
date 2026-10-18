/* The forward of ID3D11DeviceContext::Map through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 5: with a back end the game is handed
 * the back end's pointer and pitches, and the front keeps no bytes for the resource (NdMemory.kept
 * is 0 and there are no blocks); with `none` the front keeps the block.  Rule 2: the context and
 * the resource cross as their back objects; the subresource, the map type and the flags cross as
 * the game passed them; argument 6 is the pointer the back end handed out.  Rule 1: each stop of
 * the body comes before the back end is reached.  Rule 4: a result of the back end other than S_OK
 * is a stop that names the method.  Rule 6: a read mapping of a staging buffer that no call wrote
 * is the back end's to answer, so with a back end it is not a stop and the ledger of invented
 * answers counts nothing; with `none` that read is a stop. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, Map)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC d = { .ByteWidth = 64, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_VERTEX_BUFFER,
                            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *buf = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, NULL, &buf), S_OK);
    T_REQUIRE(buf != NULL);

    T_CASE("a dynamic buffer maps for writing: S_OK, its size as both pitches, memory the game can write");
    D3D11_MAPPED_SUBRESOURCE m = { 0 };
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    T_TRUE(m.pData != NULL);
    T_EQ(m.RowPitch, 64);
    T_EQ(m.DepthPitch, 64);
    memset(m.pData, 0xab, 64);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Map), 1);
        T_EQ(T_BACK_ARG(Map, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(Map, 1), T_BACK_OF(buf));
        T_EQ(T_BACK_ARG(Map, 2), 0);
        T_EQ(T_BACK_ARG(Map, 3), D3D11_MAP_WRITE_DISCARD);
        T_EQ(T_BACK_ARG(Map, 4), 0);
        T_EQ(T_BACK_ARG(Map, 6), T_BACK_PTR(m.pData));
        T_EQ(rt_nulldev_memory(buf)->kept, 0);
        T_TRUE(rt_nulldev_memory(buf)->blocks == NULL);
    } else {
        T_EQ(rt_nulldev_memory(buf)->kept, 1);
        T_TRUE(rt_nulldev_memory(buf)->blocks != NULL);
    }

    T_CASE("the stops, each before the back end: a second Map of a mapped subresource, a NULL resource, a NULL "
           "pMappedResource, flags, a subresource the buffer does not have, a read mapping of a buffer without CPU read");
    D3D11_MAPPED_SUBRESOURCE m2 = { 0 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m2));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, NULL, 0, D3D11_MAP_WRITE_DISCARD, 0, &m2));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 0, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 1, &m2));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 1, D3D11_MAP_WRITE_DISCARD, 0, &m2));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_READ, 0, &m2));

    T_CASE("after Unmap the buffer maps again: S_OK, and the back end is reached once more");
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Map), 2);
    }

    T_CASE("a read mapping of a staging buffer no call wrote: the back end has the contents, the front invents none");
    D3D11_BUFFER_DESC sd = { .ByteWidth = 64, .Usage = D3D11_USAGE_STAGING,
                             .CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *st = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &sd, NULL, &st), S_OK);
    if (T_BACK) {
        T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m), S_OK);
        T_TRUE(m.pData != NULL);
        T_EQ(rt_nulldev_invented_count(ND_INV_READBACK), 0);
    } else {
        T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m),
                "a read mapping of a subresource no call wrote");
    }

    if (T_BACK) {
        T_CASE("a result of the back end other than S_OK is a stop that names the method");
        ID3D11Buffer *b2 = NULL;
        T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, NULL, &b2), S_OK);
        rt_d3dback_record_answer(RT_BACK_Map, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)b2, 0, D3D11_MAP_WRITE_DISCARD, 0, &m),
                "the back end (record) answered 0x80004005 to ID3D11DeviceContext::Map");
        rt_d3dback_record_answer(RT_BACK_Map, S_OK);
    }

    ID3D11Resource *ns = (ID3D11Resource *)T_BACK_NEW(device, &rt_nulldev_sampler_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, ns, 0, D3D11_MAP_WRITE_DISCARD, 0, &m2));

    /* buf is still mapped from the last Map above: unmap it, so the map type is the only fault */
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, (D3D11_MAP)9, 0, &m2));

    ID3D11Buffer *dw = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, NULL, &dw), S_OK);
    T_REQUIRE(dw != NULL);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)dw, 0, D3D11_MAP_WRITE, 0, &m2));

    ID3D11Buffer *ws = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &sd, NULL, &ws), S_OK);
    T_REQUIRE(ws != NULL);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)ws, 0, D3D11_MAP_WRITE_DISCARD, 0, &m2));

    D3D11_BUFFER_DESC ro = { .ByteWidth = 64, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ };
    ID3D11Buffer *rb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &ro, NULL, &rb), S_OK);
    T_REQUIRE(rb != NULL);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)rb, 0, D3D11_MAP_WRITE, 0, &m2));

    D3D11_BUFFER_DESC cd = { .ByteWidth = 64, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                             .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *cb = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &cd, NULL, &cb), S_OK);
    T_REQUIRE(cb != NULL);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m2));
}
