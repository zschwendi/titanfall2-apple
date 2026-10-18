/* Expected values: Microsoft's pages of CreateVertexShader and CreatePixelShader (the class
 * linkage "can be NULL"; a NULL out-pointer validates and returns S_FALSE "instead of S_OK").
 * The shader is a record of its byte count (rt/nulldev.h); a new device has 2 references.  The
 * bytes are not a real shader: only their first four characters are looked at. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateVertexShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    uint8_t code[64];
    ID3D11VertexShader *shader = NULL;
    memset(code, 0x7e, sizeof code);
    memcpy(code, "DXBC", 4);

    T_CASE("bytecode of 64 bytes beginning with DXBC: S_OK, one reference, the device held, the length kept");
    T_EQ(COM_CALL(ID3D11Device, CreateVertexShader, device, code, sizeof code, NULL, &shader), S_OK);
    T_REQUIRE(shader != NULL);
    T_EQ(rt_com_refs(shader), 1);
    T_EQ(rt_com_refs(device), 3);
    const NdShader *s = rt_com_state_of(shader, &rt_nulldev_vs_class);
    T_TRUE(s->child.device == device);
    T_EQ(s->bytecode_length, 64);

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    T_EQ(COM_CALL(ID3D11Device, CreateVertexShader, device, code, sizeof code, NULL, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 3);

    T_TRAPS(COM_CALL(ID3D11Device, CreateVertexShader, device, code, sizeof code, (ID3D11ClassLinkage *)code, &shader), "with a class linkage");
    T_TRAPS(COM_CALL(ID3D11Device, CreateVertexShader, device, NULL, 64, NULL, &shader), "is NULL, shorter than 4 bytes or does not begin with DXBC");
    T_TRAPS(COM_CALL(ID3D11Device, CreateVertexShader, device, code, 3, NULL, &shader), "is NULL, shorter than 4 bytes or does not begin with DXBC");
    code[0] = 'd';
    T_TRAPS(COM_CALL(ID3D11Device, CreateVertexShader, device, code, sizeof code, NULL, &shader), "is NULL, shorter than 4 bytes or does not begin with DXBC");
    /* COM_CALL checks that `this` carries the device's vtable first, so the creator's helper is called
     * here with a buffer as its device: its first statement stops, and the stop names the class (G8). */
    ID3D11Buffer *buf = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    T_TRAPS(rt_nulldev_shader_create(T_CTX(), (ID3D11Device *)buf, &rt_nulldev_vs_class, code, sizeof code, NULL, NULL),
            "where an object of class D3D11Device is expected");
}
