/* Expected values: Microsoft's pages of ID3D11Device::CreateBuffer and D3D11_BUFFER_DESC (a
 * buffer is ByteWidth bytes; pInitialData NULL allocates space only, and cannot be NULL for
 * IMMUTABLE; a NULL ppBuffer validates and returns S_FALSE) and of D3D11_CPU_ACCESS_FLAG (write
 * access needs dynamic or staging usage, read access staging).  The counts: a new device has 2
 * references (docs/handoff/d3d-contract.md, section 3) and every object it makes holds it. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateBuffer)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC d = { 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }, bad;
    ID3D11Buffer *buf = NULL;
    uint8_t bytes[256];
    D3D11_SUBRESOURCE_DATA init = { bytes, 7, 9 };      /* the pitches mean nothing for a buffer */

    T_CASE("a buffer of 256 bytes: S_OK, one reference, the device held, the description kept as passed, one subresource");
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, NULL, &buf), S_OK);
    T_REQUIRE(buf != NULL);
    T_EQ(rt_com_refs(buf), 1);
    T_EQ(rt_com_refs(device), 3);
    NdBuffer *b = rt_com_state_of(buf, &rt_nulldev_buffer_class);
    T_TRUE(b->child.device == device);
    T_TRUE(!memcmp(&b->desc, &d, sizeof d));
    T_EQ(b->mem.nsub, 1);
    T_EQ(b->mem.size, 256);
    T_EQ(rt_com_release(buf), 0);
    T_EQ(rt_com_refs(device), 2);

    T_CASE("initial data is copied into the buffer");
    for (int i = 0; i < 256; i++) bytes[i] = (uint8_t)(i * 7 + 1);
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, &init, &buf), S_OK);
    memset(bytes, 0, sizeof bytes);                     /* a copy, not a pointer to the game's bytes */
    const NdBlock *blk = rt_nulldev_block(buf, 0);
    T_EQ(rt_nulldev_content(buf, 0), ND_CONTENT_GAME);
    for (int i = 0; i < 256; i++) T_EQ(blk->data[i], (uint8_t)(i * 7 + 1));

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object left");
    T_EQ(rt_com_refs(device), 3);
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &d, NULL, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 3);

    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, NULL, NULL, &buf), "CreateBuffer with a NULL description");
    bad = d; bad.ByteWidth = 0;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &bad, NULL, &buf), "a buffer description with a size of 0");
    bad = d; bad.Usage = D3D11_USAGE_DEFAULT;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &bad, NULL, &buf), "CPU write access without dynamic or staging usage");
    bad = d; bad.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &bad, NULL, &buf), "CPU read access without staging usage");
    bad = d; bad.Usage = D3D11_USAGE_IMMUTABLE; bad.CPUAccessFlags = 0;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &bad, NULL, &buf), "immutable usage without initial data");
    bad = d; bad.Usage = 4;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &bad, NULL, &buf), "a usage that is not one of D3D11_USAGE");
    bad = d; bad.MiscFlags = 0x2;                       /* D3D11_RESOURCE_MISC_SHARED */
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &bad, NULL, &buf), "a miscellaneous flag the device does not know");
    init.pSysMem = NULL;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &d, &init, &buf), "initial data whose pSysMem is NULL");
    /* unit d3d-fix-2: usage against CPU access and bind flags (docs/reviews/d3d-format-table.md, point 4, C1, C2) */
    D3D11_BUFFER_DESC c1 = { 64, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &c1, NULL, &buf), "dynamic usage without CPU write access");
    c1 = (D3D11_BUFFER_DESC){ 64, D3D11_USAGE_STAGING, 0, 0, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &c1, NULL, &buf), "staging usage without CPU access");
    c1 = (D3D11_BUFFER_DESC){ 64, D3D11_USAGE_STAGING, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_READ, 0, 0 };
    T_TRAPS(COM_CALL(ID3D11Device, CreateBuffer, device, &c1, NULL, &buf), "staging usage with a bind flag");
}
