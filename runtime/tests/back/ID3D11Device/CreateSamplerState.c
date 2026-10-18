/* The forward of ID3D11Device::CreateSamplerState through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object
 * and the game's description by pointer, unchanged.  Rule 3: a new state object gets a back object
 * from the creator's out-pointer, and its last Release releases that back object, once.  Rule 7:
 * a state object that already exists (an equal description, as the nulldev test's
 * CreateSamplerState has it) is answered by the front alone: the back end is not asked.  Rule 4: a
 * result of the back end that differs from the front's S_OK is a stop that names the method.
 * Rule 1: every stop of the body comes before the back end is reached.  E_FAIL is 0x80004005. */
#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateSamplerState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    /* the game's first: point filter, clamp, anisotropy 1, comparison ALWAYS (gate3-run-2.md, entry 5) */
    D3D11_SAMPLER_DESC first = { 0, 3, 3, 3, 0.0f, 1, 8, { 0, 0, 0, 0 }, 0.0f, 0.0f };
    ID3D11SamplerState *a = NULL, *same = NULL, *other = NULL;

    T_CASE("a new sampler state: S_OK, and the back end is asked once with the game's description by pointer");
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, &a), S_OK);
    T_REQUIRE(a != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateSamplerState), 1);
        T_EQ(T_BACK_ARG(CreateSamplerState, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateSamplerState, 1), T_BACK_PTR(&first));
        T_EQ(T_BACK_ARG(CreateSamplerState, 2), T_BACK_OF(a));
        T_TRUE(T_BACK_OF(a) != 0);
    }

    T_CASE("the same description gives the same object with one more reference, and the front answers alone: the back end is not asked again");
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, &same), S_OK);
    T_TRUE(same == a);
    T_EQ(rt_com_refs(a), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateSamplerState), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateSamplerState), 1);
    }

    T_CASE("a different description (MaxLOD 4) gives another object, with its own back object");
    D3D11_SAMPLER_DESC other_desc = first;
    other_desc.MaxLOD = 4.0f;
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &other_desc, &other), S_OK);
    T_TRUE(other != a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateSamplerState), 2);
        T_EQ(T_BACK_ARG(CreateSamplerState, 1), T_BACK_PTR(&other_desc));
        T_EQ(T_BACK_ARG(CreateSamplerState, 2), T_BACK_OF(other));
        T_TRUE(T_BACK_OF(other) != 0 && T_BACK_OF(other) != T_BACK_OF(a));
    }

    T_CASE("the last Release of the sampler releases its back object, once");
    T_EQ(COM_CALL(ID3D11SamplerState, Release, same), 1);       /* the reference the equal description added */
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t was = T_BACK ? T_BACK_OF(a) : 0;
    T_EQ(COM_CALL(ID3D11SamplerState, Release, a), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 0), RT_BK_SAMPLER);
        T_EQ(T_BACK_ARG(Release, 1), was);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        D3D11_SAMPLER_DESC third_desc = first;
        third_desc.MaxLOD = 2.0f;                               /* a third, new description */
        ID3D11SamplerState *third = NULL;
        rt_d3dback_record_answer(RT_BACK_CreateSamplerState, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateSamplerState, device, &third_desc, &third),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateSamplerState");
        rt_d3dback_record_answer(RT_BACK_CreateSamplerState, S_OK);
    }

    T_CASE("the stops of the body come before the back end: a NULL description, a NULL out-pointer, a filter that is not one of D3D11_FILTER, "
           "an address mode that is not one of D3D11_TEXTURE_ADDRESS_MODE, a maximum anisotropy above 16, and an object of another class as the device");
    ID3D11SamplerState *stop = NULL;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateSamplerState, device, NULL, &stop));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, NULL));
    D3D11_SAMPLER_DESC bad = first;
    bad.Filter = 2;                                             /* not one of D3D11_FILTER */
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateSamplerState, device, &bad, &stop));
    bad = first;
    bad.AddressV = 6;                                           /* not one of D3D11_TEXTURE_ADDRESS_MODE */
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateSamplerState, device, &bad, &stop));
    bad = first;
    bad.Filter = 0x55;
    bad.MaxAnisotropy = 17;                                     /* above 16 */
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateSamplerState, device, &bad, &stop));
    /* COM_CALL fails the test on a `this` of another class (its vtable check), so the creator's helper is called here
     * with a query as its device, as CreateVertexShader's test does.  Plain T_TRAPS, not T_BACK_STOPS_BEFORE: the seam's
     * "had not reached the back end" line is added only to a report made inside a method, and this stop is the helper's
     * first check, reached before the body's RT_BACK_CREATE, so it cannot have reached the back end. */
    T_TRAPS(rt_nulldev_state_create(T_CTX(), (ID3D11Device *)T_BACK_NEW(device, &rt_nulldev_query_class),
                                    &rt_nulldev_sampler_class, &first, (void **)&stop),
            "where an object of class D3D11Device is expected");
}
