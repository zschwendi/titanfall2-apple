/* The forward of ID3D11Device::CreateUnorderedAccessView through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device and the resource cross as
 * their back objects, a NULL description as NULL, and the game's description by pointer.  Rule 3:
 * the view gets a back object of its own (the creator's out-pointer), and its destruction releases
 * it, once.  Rule 7: a call that only validates (a NULL out-pointer: S_FALSE) makes no view and is
 * answered by the front alone.  Rule 4: a result of the back end that differs from the front's
 * S_OK is a stop that names the method.  Rule 1: each stop the creator reaches (a NULL resource, a
 * resource of another class, a view dimension a texture 2D does not have, a NULL description on a
 * 3D texture) stops before the back end is reached.  The resource is a 2D texture made by
 * rt_nulldev_texture_new (the description the nulldev test's CreateUnorderedAccessView uses) and
 * given its back object by T_BACK_ADOPT. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateUnorderedAccessView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    ID3D11Texture2D *tex = rt_nulldev_texture_new(T_CTX(), device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &td, false);
    tex = T_BACK_ADOPT(tex);
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture3D *vol = rt_nulldev_texture_new(T_CTX(), device, D3D11_RESOURCE_DIMENSION_TEXTURE3D, &vd, false);
    vol = T_BACK_ADOPT(vol);
    D3D11_UNORDERED_ACCESS_VIEW_DESC d, bad;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 2;
    ID3D11UnorderedAccessView *view = NULL, *second = NULL;

    T_CASE("a view with a NULL description: S_OK, a view; the device and the resource cross as their back objects, NULL as NULL");
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, NULL, &view), S_OK);
    T_REQUIRE(view != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateUnorderedAccessView), 1);
        T_EQ(T_BACK_ARG(CreateUnorderedAccessView, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateUnorderedAccessView, 1), T_BACK_OF(tex));
        T_EQ(T_BACK_ARG(CreateUnorderedAccessView, 2), 0);
        T_EQ(T_BACK_ARG(CreateUnorderedAccessView, 3), T_BACK_OF(view));
        T_TRUE(T_BACK_OF(view) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(view)), RT_BK_UAV);
    }

    T_CASE("a view with a description: the game's description crosses by pointer, and the view gets its own back object");
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &d, &second), S_OK);
    T_REQUIRE(second != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateUnorderedAccessView), 2);
        T_EQ(T_BACK_ARG(CreateUnorderedAccessView, 2), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateUnorderedAccessView, 3), T_BACK_OF(second));
        T_TRUE(T_BACK_OF(second) != 0 && T_BACK_OF(second) != T_BACK_OF(view));
    }

    T_CASE("a call that only validates (NULL out-pointer) returns S_FALSE and is answered by the front alone");
    T_EQ(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateUnorderedAccessView), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateUnorderedAccessView), 1);
    }

    T_CASE("the first view's Release releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t live = T_BACK ? rt_d3dback_record_live(RT_BK_UAV) : 0;
    T_EQ(COM_CALL(ID3D11UnorderedAccessView, Release, view), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(rt_d3dback_record_live(RT_BK_UAV), live - 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_CreateUnorderedAccessView, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &d, &second),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateUnorderedAccessView");
        rt_d3dback_record_answer(RT_BACK_CreateUnorderedAccessView, S_OK);
    }

    T_CASE("a NULL resource stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, NULL, &d, &second));

    T_CASE("a resource of another class (a query) stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device,
                                 (ID3D11Resource *)T_BACK_NEW(device, &rt_nulldev_query_class), &d, &second));

    T_CASE("a view dimension a texture 2D does not have stops before the back end is reached");
    bad = d;
    bad.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)tex, &bad, &second));

    T_CASE("a NULL description on a 3D texture stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateUnorderedAccessView, device, (ID3D11Resource *)vol, NULL, &second));
}
