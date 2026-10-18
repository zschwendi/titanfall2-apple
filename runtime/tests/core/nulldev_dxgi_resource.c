/* IDXGIResource on the four resource classes (unit d3d-fix-2; E8 of
 * docs/reviews/d3d-contract-check.md, whose test this is).
 *
 * Expected values: Microsoft documents IDXGIResource on every Direct3D 11 resource, and Wine's
 * d3d11 test test_dxgi_resource finds IDXGIDeviceSubObject and IDXGIObject answered with the
 * IDXGIResource pointer (both as that block reads them).  The three IIDs are the MinGW-w64
 * dxgi.h's (runtime/com/desc/IDXGIResource.idef carries them).  That a 2D texture with more
 * than one mip level refuses IDXGISurface is the object model's rule (rt/nulldev.h). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

static const GUID iid_object    = { 0xaec22fb8, 0x76f3, 0x4639, { 0x9b, 0xe0, 0x28, 0xeb, 0x43, 0xa6, 0x7a, 0x2e } };
static const GUID iid_subobject = { 0x3d3e0379, 0xf9de, 0x4d58, { 0xbb, 0x6c, 0x18, 0xd6, 0x29, 0x92, 0xf1, 0xa6 } };

static void check(void *resource, const RtComClass *cls) {
    void *res = NULL, *sub = NULL, *obj = NULL;
    LONG before = rt_com_refs(resource);
    T_EQ(rt_com_QueryInterface(T_CTX(), resource, &rt_iface_IDXGIResource.iid, &res), S_OK);
    T_REQUIRE(res != NULL);
    T_TRUE(res != resource);                            /* another face of the same object */
    T_TRUE(rt_com_class_of(res) == cls);
    T_EQ(rt_com_refs(resource), before + 1);
    T_EQ(rt_com_QueryInterface(T_CTX(), resource, &iid_subobject, &sub), S_OK);
    T_EQ(rt_com_QueryInterface(T_CTX(), resource, &iid_object, &obj), S_OK);
    T_TRUE(sub == res);
    T_TRUE(obj == res);
    T_EQ(rt_com_refs(resource), before + 3);
}

TEST(core, nulldev_dxgi_resource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a buffer answers IDXGIResource with a reference, and its two bases with the same pointer");
    ID3D11Buffer *buffer = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    check(buffer, &rt_nulldev_buffer_class);

    T_CASE("the same for a 1D and a 3D texture");
    check(rt_nulldev_new(device, &rt_nulldev_texture1d_class), &rt_nulldev_texture1d_class);
    check(rt_nulldev_new(device, &rt_nulldev_texture3d_class), &rt_nulldev_texture3d_class);

    T_CASE("a 2D texture with mip levels: the bases come back as the IDXGIResource pointer, and IDXGISurface is still refused");
    D3D11_TEXTURE2D_DESC td = { 64, 64, 7, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *tex = NULL;
    void *surface = (void *)(uintptr_t)1;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &tex), S_OK);
    check(tex, &rt_nulldev_texture2d_class);
    T_EQ(rt_com_QueryInterface(T_CTX(), tex, &rt_iface_IDXGISurface.iid, &surface), E_NOINTERFACE);
    T_TRUE(surface == NULL);

    T_CASE("a plain 2D texture presents both IDXGIResource and IDXGISurface");
    td.MipLevels = 1;
    ID3D11Texture2D *plain = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &plain), S_OK);
    check(plain, &rt_nulldev_texture2d_class);
    T_EQ(rt_com_QueryInterface(T_CTX(), plain, &rt_iface_IDXGISurface.iid, &surface), S_OK);
    T_TRUE(surface != NULL && surface != plain);

    T_CASE("the four methods of IDXGIResource are stops that name themselves");
    IDXGIResource *res = rt_com_face(buffer, &rt_iface_IDXGIResource);
    UINT usage = 0, priority = 0;
    HANDLE shared = NULL;
    T_TRAPS(COM_CALL(IDXGIResource, GetUsage, res, &usage), "IDXGIResource::GetUsage");
    T_TRAPS(COM_CALL(IDXGIResource, GetSharedHandle, res, &shared), "IDXGIResource::GetSharedHandle");
    T_TRAPS(COM_CALL(IDXGIResource, SetEvictionPriority, res, 0), "IDXGIResource::SetEvictionPriority");
    T_TRAPS(COM_CALL(IDXGIResource, GetEvictionPriority, res, &priority), "IDXGIResource::GetEvictionPriority");
}
