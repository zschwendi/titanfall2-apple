/* Expected values: Microsoft's page of ID3D11DeviceContext::Unmap ("Invalidate the pointer to
 * a resource and reenable the GPU's access to that resource"): afterwards the subresource can
 * be mapped again.  That the bytes stay is this device's rule (Map.c): a later mapping finds
 * what the game wrote.  The three stops are this device's choice where the page is silent. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, Unmap)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC stg = { 64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ID3D11Buffer *buf = NULL;
    D3D11_MAPPED_SUBRESOURCE m, again;
    static const uint8_t first[64] = { 1, 2, 3 };
    D3D11_SUBRESOURCE_DATA init = { first, 0, 0 };      /* all of it the game's: only then may it be mapped for reading */
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &stg, &init, &buf), S_OK);

    T_CASE("after Unmap the subresource can be mapped again and the count of mapped subresources is 0");
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_READ_WRITE, 0, &m), S_OK);
    T_EQ(rt_nulldev_memory(buf)->mapped, 1);
    memset(m.pData, 0x5c, 64);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);
    T_EQ(rt_nulldev_memory(buf)->mapped, 0);
    T_EQ(rt_nulldev_block(buf, 0)->map_type, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)buf, 0, D3D11_MAP_READ, 0, &again), S_OK);

    T_CASE("the bytes stay where they were");
    T_TRUE(again.pData == m.pData);
    for (int i = 0; i < 64; i++) T_EQ(((uint8_t *)again.pData)[i], 0x5c);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, Unmap, context, NULL, 0), "Unmap of a NULL resource");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 1), "Unmap of a subresource the resource does not have: 1 of 1");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)buf, 0), "Unmap of a subresource that is not mapped");
}
