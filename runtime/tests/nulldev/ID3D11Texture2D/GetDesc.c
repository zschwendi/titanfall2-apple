/* Expected values, none from the body.  The eleven numbers are the test's own: it puts them into
 * the texture's description field by field, and the 44 bytes it expects are those numbers laid out
 * in the order of D3D11_TEXTURE2D_DESC (rt/win/d3d11.h: 44 bytes, Width first, MiscFlags last).
 * The 0xa5 guard bytes after the 44 are the test's own.  That GetDesc returns the description the
 * texture was created with is Microsoft's page of ID3D11Texture2D::GetDesc (read 2026-10-10: "Get
 * the properties of the texture resource."); the stop is the notes' rule: the page names no result
 * for a NULL pDesc. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Texture2D, GetDesc)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Texture2D *tex = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    NdTexture *t = rt_com_state_of(tex, &rt_nulldev_texture2d_class);
    t->desc.d2.Width = 101;
    t->desc.d2.Height = 102;
    t->desc.d2.MipLevels = 103;
    t->desc.d2.ArraySize = 104;
    t->desc.d2.Format = 105;
    t->desc.d2.SampleDesc.Count = 106;
    t->desc.d2.SampleDesc.Quality = 107;
    t->desc.d2.Usage = 108;
    t->desc.d2.BindFlags = 109;
    t->desc.d2.CPUAccessFlags = 110;
    t->desc.d2.MiscFlags = 111;

    uint32_t fields[11] = { 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111 };
    uint8_t raw[44 + 8], want[44];
    memcpy(want, fields, sizeof want);

    T_CASE("the description the texture was created with, all 44 bytes, and nothing after them");
    memset(raw, 0xa5, sizeof raw);
    COM_CALL(ID3D11Texture2D, GetDesc, tex, (D3D11_TEXTURE2D_DESC *)raw);
    T_TRUE(!memcmp(raw, want, 44));
    for (int i = 44; i < 52; i++) T_EQ(raw[i], 0xa5);

    T_TRAPS(COM_CALL(ID3D11Texture2D, GetDesc, tex, NULL), "GetDesc with a NULL description");
}
