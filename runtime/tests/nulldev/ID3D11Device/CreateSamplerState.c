/* Expected values: Microsoft's page of the creator (an equal state gives the same interface)
 * and Wine's d3d11 conformance tests as the contract's check reads them (docs/reviews/
 * d3d-contract-check.md, E5): what Windows normalises before it compares, and that the second
 * create returns the same object with one more reference.  The counts: a new device has 2
 * references and each object it makes holds it (docs/handoff/d3d-contract.md, section 3). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateSamplerState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    /* the game's first: point filter, clamp, anisotropy 1, comparison ALWAYS (gate3-run-2.md, entry 5) */
    D3D11_SAMPLER_DESC first = { 0, 3, 3, 3, 0.0f, 1, 8, { 0, 0, 0, 0 }, 0.0f, 0.0f }, bad;
    ID3D11SamplerState *a = NULL, *b = NULL, *c = NULL, *e = NULL;

    T_CASE("the game's first sampler: S_OK, one reference, the device held");
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, &a), S_OK);
    T_REQUIRE(a != NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(device), 3);
    T_TRUE(((NdStateObject *)rt_com_state_of(a, &rt_nulldev_sampler_class))->child.device == device);

    T_CASE("an equal description gives the same object with one more reference");
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(device), 3);
    T_EQ(rt_com_release(b), 1);

    T_CASE("a linear, WRAP sampler is normalised: MaxAnisotropy 0, ComparisonFunc NEVER, BorderColor zeros, so MaxAnisotropy 16 and 1 are the same object");
    D3D11_SAMPLER_DESC lin = { 0x15, 1, 1, 1, 0.0f, 16, 8, { 0, 1, 0, 1 }, 0.0f, 16.0f }, lin1;
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &lin, &c), S_OK);
    const D3D11_SAMPLER_DESC *kept = &((NdStateObject *)rt_com_state_of(c, &rt_nulldev_sampler_class))->desc.sampler;
    T_EQ(kept->Filter, 0x15);
    T_EQ(kept->AddressU, 1);
    T_EQ(kept->MaxAnisotropy, 0);
    T_EQ(kept->ComparisonFunc, 1);
    for (int i = 0; i < 4; i++) T_TRUE(kept->BorderColor[i] == 0.0f);
    T_TRUE(kept->MaxLOD == 16.0f);
    lin1 = lin; lin1.MaxAnisotropy = 1;
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &lin1, &e), S_OK);
    T_TRUE(e == c);
    T_EQ(rt_com_refs(c), 2);

    T_CASE("a different description gives another object");
    T_TRUE(c != a);
    lin1.MaxLOD = 4.0f;
    ID3D11SamplerState *other = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &lin1, &other), S_OK);
    T_TRUE(other != c && other != a);
    T_EQ(rt_com_refs(other), 1);

    T_CASE("after the last Release an equal description gives a new object");
    uint64_t serial = rt_com_serial(a);
    T_EQ(rt_com_release(a), 0);
    T_EQ(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, &a), S_OK);
    T_TRUE(rt_com_serial(a) != serial);
    T_EQ(rt_com_refs(a), 1);

    T_TRAPS(COM_CALL(ID3D11Device, CreateSamplerState, device, NULL, &b), "a state creator with a NULL description or a NULL out-pointer");
    T_TRAPS(COM_CALL(ID3D11Device, CreateSamplerState, device, &first, NULL), "a state creator with a NULL description or a NULL out-pointer");
    bad = first; bad.Filter = 2;
    T_TRAPS(COM_CALL(ID3D11Device, CreateSamplerState, device, &bad, &b), "a filter that is not one of D3D11_FILTER");
    bad = first; bad.AddressV = 6;
    T_TRAPS(COM_CALL(ID3D11Device, CreateSamplerState, device, &bad, &b), "an address mode that is not one of D3D11_TEXTURE_ADDRESS_MODE");
    bad = first; bad.Filter = 0x55; bad.MaxAnisotropy = 17;
    T_TRAPS(COM_CALL(ID3D11Device, CreateSamplerState, device, &bad, &b), "a maximum anisotropy above 16");
}
