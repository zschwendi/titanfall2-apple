/* Expected values: Microsoft's page of IDXGIFactory1::EnumAdapters1 (read 2026-10-10: S_OK
 * with a new reference, DXGI_ERROR_NOT_FOUND from the adapter count on, DXGI_ERROR_INVALID_CALL
 * for a NULL out-pointer; the numbers of the two codes are MinGW's, rt/win/dxgi.h); the
 * count of 1 is finding 35's; NULL in the out-pointer on "not found" is the contract's choice. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIFactory1, EnumAdapters1)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *own = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIFactory1 *factory = ((NdAdapter *)rt_com_state_of(own, &rt_nulldev_adapter_class))->factory;
    IDXGIAdapter1 *a = NULL, *b = NULL;

    T_CASE("index 0 is the one adapter, a new object with one reference");
    T_EQ(rt_com_refs(factory), 1);
    T_EQ(COM_CALL(IDXGIFactory1, EnumAdapters1, factory, 0, &a), S_OK);
    T_TRUE(rt_com_class_of(a) == &rt_nulldev_adapter_class);
    T_EQ(rt_com_refs(a), 1);
    T_TRUE(((NdAdapter *)rt_com_state_of(a, &rt_nulldev_adapter_class))->factory == factory);
    T_EQ(rt_com_refs(factory), 2);                      /* the new adapter holds its factory */
    T_EQ(COM_CALL(IDXGIFactory1, EnumAdapters1, factory, 0, &b), S_OK);
    T_TRUE(b != a && b != own);
    T_EQ(COM_CALL(IDXGIAdapter1, Release, b), 0);
    T_EQ(rt_com_refs(factory), 2);                      /* a destroyed adapter lets go of it */

    T_CASE("index 1 and above is DXGI_ERROR_NOT_FOUND");
    b = a;
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, EnumAdapters1, factory, 1, &b), 0x887a0002u);
    T_TRUE(b == NULL);
    b = a;
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, EnumAdapters1, factory, 0xffffffffu, &b), 0x887a0002u);
    T_TRUE(b == NULL);

    T_CASE("a NULL out-pointer is DXGI_ERROR_INVALID_CALL");
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, EnumAdapters1, factory, 0, NULL), 0x887a0001u);
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, EnumAdapters1, factory, 7, NULL), 0x887a0001u);
    T_EQ(rt_com_refs(factory), 2);
}
