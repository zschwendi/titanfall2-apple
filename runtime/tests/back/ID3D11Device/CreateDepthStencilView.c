/* The forward of ID3D11Device::CreateDepthStencilView through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device and the resource cross as
 * their back objects, a NULL description as NULL, and the game's description by pointer.  Rule 3:
 * the view gets a back object of its own (the creator's out-pointer), and its destruction releases
 * it, once.  Rule 7: a call that only validates (a NULL out-pointer: S_FALSE) makes no view and is
 * answered by the front alone.  Rule 4: a result of the back end that differs from the front's
 * S_OK is a stop that names the method.  Rule 1: each stop of the front comes before the back end
 * is reached: a NULL resource, a resource of the wrong class, a resource of the wrong kind for a
 * view, a NULL description that cannot be derived, and a device of the wrong class.  The resource
 * is a 2D depth-stencil texture of a typed format (so a NULL description can stand for it) made by
 * rt_nulldev_texture_new, as the SRV test makes its texture, and given its back object by
 * T_BACK_ADOPT.  The stops' resources are the ones the nulldev test asserts (its T_TRAPS). */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(ID3D11Device, CreateDepthStencilView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC td = { 64, 32, 3, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
    ID3D11Texture2D *tex = rt_nulldev_texture_new(T_CTX(), device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &td, false);
    tex = T_BACK_ADOPT(tex);
    D3D11_DEPTH_STENCIL_VIEW_DESC d;
    memset(&d, 0, sizeof d);
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 1;
    ID3D11DepthStencilView *view = NULL, *second = NULL;

    T_CASE("a view with a NULL description: S_OK, a view in the resource's format; the device and the resource cross as their back objects, NULL as NULL");
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, NULL, &view), S_OK);
    T_REQUIRE(view != NULL);
    const NdView *v = rt_com_state_of(view, &rt_nulldev_dsv_class);
    T_EQ(v->desc.dsv.Format, DXGI_FORMAT_D24_UNORM_S8_UINT);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateDepthStencilView), 1);
        T_EQ(T_BACK_ARG(CreateDepthStencilView, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(CreateDepthStencilView, 1), T_BACK_OF(tex));
        T_EQ(T_BACK_ARG(CreateDepthStencilView, 2), 0);
        T_EQ(T_BACK_ARG(CreateDepthStencilView, 3), T_BACK_OF(view));
        T_TRUE(T_BACK_OF(view) != 0);
        T_EQ(rt_d3dback_record_kind(rt_d3dback_of(view)), RT_BK_DSV);
    }

    T_CASE("a view with a description: the game's description crosses by pointer, and the view gets its own back object");
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, &d, &second), S_OK);
    T_REQUIRE(second != NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateDepthStencilView), 2);
        T_EQ(T_BACK_ARG(CreateDepthStencilView, 2), T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateDepthStencilView, 3), T_BACK_OF(second));
        T_TRUE(T_BACK_OF(second) != 0 && T_BACK_OF(second) != T_BACK_OF(view));
    }

    T_CASE("a call that only validates (NULL out-pointer) returns S_FALSE and is answered by the front alone");
    T_EQ(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, &d, NULL), S_FALSE);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateDepthStencilView), 2);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateDepthStencilView), 1);
    }

    T_CASE("the first view's Release releases its back object, once");
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;
    uint64_t live = T_BACK ? rt_d3dback_record_live(RT_BK_DSV) : 0;
    T_EQ(COM_CALL(ID3D11DepthStencilView, Release, view), 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(rt_d3dback_record_live(RT_BK_DSV), live - 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_CreateDepthStencilView, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)tex, &d, &second),
                "the back end (record) answered 0x80004005 to ID3D11Device::CreateDepthStencilView");
        rt_d3dback_record_answer(RT_BACK_CreateDepthStencilView, S_OK);
    }

    T_CASE("a NULL resource stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilView, device, NULL, &d, &second));

    T_CASE("a resource that is not an object of the front's class stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)T_BACK_NEW(device, &rt_nulldev_query_class), &d, &second));

    T_CASE("a view on a texture 3D stops before the back end is reached");
    D3D11_TEXTURE3D_DESC vd = { 8, 8, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture3D *vol = rt_nulldev_texture_new(T_CTX(), device, D3D11_RESOURCE_DIMENSION_TEXTURE3D, &vd, false);
    vol = T_BACK_ADOPT(vol);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)vol, &d, &second));

    T_CASE("a NULL description on a buffer stops before the back end is reached");
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0 };
    ID3D11Buffer *buf = rt_nulldev_buffer_new(T_CTX(), device, &bd, false);
    buf = T_BACK_ADOPT(buf);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)buf, NULL, &second));

    T_CASE("a NULL description on a resource of a typeless format stops before the back end is reached");
    D3D11_TEXTURE2D_DESC tl = td;
    tl.Format = DXGI_FORMAT_R24G8_TYPELESS;
    ID3D11Texture2D *typeless = rt_nulldev_texture_new(T_CTX(), device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &tl, false);
    typeless = T_BACK_ADOPT(typeless);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11Device, CreateDepthStencilView, device, (ID3D11Resource *)typeless, NULL, &second));

    /* COM_CALL dispatches through the vtable of its object, so a device of the wrong class cannot
     * reach the body through it: the creator the body calls is given the object directly (as the
     * nulldev test of CreateShaderResourceView does).  Its check is the creator's first statement,
     * so the stop comes before the back end.  But its report (core/com.c:211) does not end with the
     * seam's line (rt/d3dback.h, rule 1), so it is not a T_BACK_STOPS_BEFORE: a T_TRAPS in both halves. */
    T_CASE("a device that is not an object of the front's class stops, in both halves");
    T_TRAPS(rt_nulldev_view_create(T_CTX(), (ID3D11Device *)T_BACK_NEW(device, &rt_nulldev_query_class), &rt_nulldev_dsv_class,
                                   (ID3D11Resource *)tex, &d, (void **)&second),
            "where an object of class D3D11Device is expected");
}
