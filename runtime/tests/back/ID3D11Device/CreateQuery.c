/* The forward of ID3D11Device::CreateQuery through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 3: the creator makes the front object,
 * asks the back end for its own (entry CreateQuery: the back end's device, then the game's
 * description by pointer, unchanged) and keeps it; the front object's destruction releases it,
 * once (entry Release, kind RT_BK_QUERY).  Rule 7: a call that only validates (a NULL
 * out-pointer: S_FALSE, CreateQuery's page) makes no object and is answered by the front alone.
 * Rule 4: a result of the back end that differs from the front's S_OK is a stop that names the
 * method and both results.  Rule 1: each stop of the body comes before the back end.
 * The kinds are D3D11_QUERY (rt/win/d3d11.h): event 0, occlusion 1, timestamp 2, timestamp
 * disjoint 3; kind 4 (pipeline statistics) is none of the four the body keeps.  The flag
 * D3D11_QUERY_MISC_PREDICATEHINT is not defined in rt/win/d3d11.h, so the test writes its value
 * (0x1) out as a literal.  E_FAIL is 0x80004005 (rt/win/error.h). */
#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateQuery)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_QUERY_DESC d = { .Query = D3D11_QUERY_OCCLUSION };
    D3D11_QUERY_DESC e = { .Query = D3D11_QUERY_EVENT };
    ID3D11Query *query = NULL;
    ID3D11Query *refused = NULL;

    T_CASE("a new occlusion query gets a back object from the game's description");
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, &query), S_OK);
    T_REQUIRE(query != NULL);
    T_TRUE(rt_com_class_of(query) == &rt_nulldev_query_class);
    T_EQ(((NdQuery *)rt_com_state_of(query, &rt_nulldev_query_class))->desc.Query, D3D11_QUERY_OCCLUSION);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateQuery), 1);
        T_EQ(T_BACK_ARG(CreateQuery, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateQuery, 1), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateQuery, 2), T_BACK_OF(query));
        T_TRUE(T_BACK_OF(query) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(query)), RT_BK_QUERY);
        T_EQ(rt_d3dback_record_live(RT_BK_QUERY), 1);
    } else {
        T_TRUE(((NdQuery *)rt_com_state_of(query, &rt_nulldev_query_class))->child.back == NULL);
    }

    T_CASE("an event query is a second object, with its own back object");
    ID3D11Query *event = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &e, &event), S_OK);
    T_REQUIRE(event != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateQuery), 2);
        T_EQ(T_BACK_ARG(CreateQuery, 1), T_BACK_PTR(&e));
        T_EQ(T_BACK_ARG(CreateQuery, 2), T_BACK_OF(event));
        T_TRUE(T_BACK_OF(event) != T_BACK_OF(query));
        T_EQ(rt_d3dback_record_made(RT_BK_QUERY), 2);
    }

    T_CASE("a call that only validates makes no back object");
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateQuery), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateQuery), 1);
        T_EQ(rt_d3dback_record_made(RT_BK_QUERY), 2);
    }

    T_CASE("the last Release of the event query releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(event) : 0;
    T_EQ(COM_CALL(ID3D11Query, Release, event), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_QUERY);
        T_EQ(T_BACK_ARG(Release, 1), was);
        T_EQ(rt_d3dback_record_live(RT_BK_QUERY), 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's is a stop that names both");
        rt_d3dback_record_answer(RT_BACK_CreateQuery, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateQuery, device, &d, &refused),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateQuery");
        rt_d3dback_record_answer(RT_BACK_CreateQuery, S_OK);
        T_EQ(T_BACK_COUNT(CreateQuery), 2);
    }

    T_CASE("each stop of the body comes before the back end");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateQuery, device, NULL, &refused));
    D3D11_QUERY_DESC kind4 = { .Query = D3D11_QUERY_PIPELINE_STATISTICS };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateQuery, device, &kind4, &refused));
    D3D11_QUERY_DESC flagged = { .Query = D3D11_QUERY_OCCLUSION, .MiscFlags = 0x1 };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateQuery, device, &flagged, &refused));
    /* Not tested here: a device of another class (rt_com_state, the body's first statement). COM_CALL
     * dispatches through the device's vtable, so such an object never reaches the body through it, and
     * the body's direct entry impl_ID3D11Device_CreateQuery has no prototype in any header. */
}
