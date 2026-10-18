/* The forward of ID3D11Device::CreateShaderResourceView through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device and the resource cross as
 * their back objects, a NULL description as NULL, and the game's description by pointer.  Rule 3:
 * the view gets a back object of its own (the creator's out-pointer), and its destruction releases
 * it, once.  Rule 7: a call that only validates (a NULL out-pointer: S_FALSE) makes no view and is
 * answered by the front alone.  Rule 4: a result of the back end that differs from the front's
 * S_OK is a stop that names the method.  Rule 1: a NULL resource stops before the back end is
 * reached.  The resource is a 2D texture made by rt_nulldev_texture_new (the description the
 * nulldev test's CreateTexture2D uses) and given its back object by T_BACK_ADOPT. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateShaderResourceView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *tex = rt_nulldev_texture_new(T_CTX(), device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &td, false);
    tex = T_BACK_ADOPT(tex);
    D3D11_SHADER_RESOURCE_VIEW_DESC d;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    d.Texture2D.MostDetailedMip = 1;
    d.Texture2D.MipLevels = 2;
    ID3D11ShaderResourceView *view = NULL, *second = NULL;

    T_CASE("a view with a NULL description: S_OK, a view; the device and the resource cross as their back objects, NULL as NULL");
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, NULL, &view), S_OK);
    T_REQUIRE(view != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateShaderResourceView), 1);
        T_EQ(T_BACK_ARG(CreateShaderResourceView, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateShaderResourceView, 1), T_BACK_OF(tex));
        T_EQ(T_BACK_ARG(CreateShaderResourceView, 2), 0);
        T_EQ(T_BACK_ARG(CreateShaderResourceView, 3), T_BACK_OF(view));
        T_TRUE(T_BACK_OF(view) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(view)), RT_BK_SRV);
    }

    T_CASE("a view with a description: the game's description crosses by pointer, and the view gets its own back object");
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, &d, &second), S_OK);
    T_REQUIRE(second != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateShaderResourceView), 2);
        T_EQ(T_BACK_ARG(CreateShaderResourceView, 2), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateShaderResourceView, 3), T_BACK_OF(second));
        T_TRUE(T_BACK_OF(second) != 0 && T_BACK_OF(second) != T_BACK_OF(view));
    }

    T_CASE("a call that only validates (NULL out-pointer) returns S_FALSE and is answered by the front alone");
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateShaderResourceView), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateShaderResourceView), 1);
    }

    T_CASE("the first view's Release releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t live = T_BACK ? rt_d3dback_record_live(RT_BK_SRV) : 0;
    T_EQ(COM_CALL(ID3D11ShaderResourceView, Release, view), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(rt_d3dback_record_live(RT_BK_SRV), live - 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_CreateShaderResourceView, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)tex, &d, &second),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateShaderResourceView");
        rt_d3dback_record_answer(RT_BACK_CreateShaderResourceView, S_OK);
    }

    T_CASE("a NULL resource stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateShaderResourceView, device, NULL, &d, &second));
}
