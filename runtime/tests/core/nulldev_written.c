/* Which units of a subresource are the game's (unit d3d-fix-3; finding 3 of
 * docs/reviews/night-2026-10-11-code-review.md): the device's record held against a model.
 *
 * Expected values: the model is the rule itself, kept one flag per unit by this test: a box
 * of UpdateSubresource makes its units the game's, a box copied from a subresource nothing was
 * put in makes them nobody's, a box copied from another subresource carries each unit's flag
 * with it (Microsoft's pages of the two methods say which units a box covers: bytes of a
 * buffer, texels of a texture; memory.c's head quotes them).  The device must agree after
 * every step in the number of units, in the kind (GAME exactly when every unit is the game's,
 * NONE when none is), and in whether a read mapping is taken.  The boxes come from a fixed
 * linear congruential sequence, so the test is the same every run. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

static uint32_t seed = 0x2545f491u;
static uint32_t rnd(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; }

#define W 13u
#define H 7u
#define N (W * H)

static void check(ID3D11Texture2D *t, const uint8_t *model, int step) {
    uint64_t total, expect = 0, known = rt_nulldev_known_units(t, 0, &total);
    for (unsigned i = 0; i < N; i++) expect += model[i];
    if (known != expect || total != N) { T_EQ(step, -1); T_EQ(known, expect); }
    unsigned kind = rt_nulldev_content(t, 0);
    if (kind != (expect == N ? ND_CONTENT_GAME : expect ? ND_CONTENT_PART : ND_CONTENT_NONE)) { T_EQ(step, -1); T_EQ(kind, 99); }
    if ((kind == ND_CONTENT_PART) != (rt_nulldev_block(t, 0)->written != NULL)) { T_EQ(step, -1); T_EQ(kind, 98); }
}

TEST(core, nulldev_written)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_TEXTURE2D_DESC sd = { W, H, 1, 1, DXGI_FORMAT_R8G8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0 };
    ID3D11Texture2D *a = NULL, *b = NULL, *empty = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &a), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &b), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &sd, NULL, &empty), S_OK);
    static uint8_t ma[N], mb[N], data[N * 2];
    ID3D11Texture2D *tex[2] = { a, b };
    uint8_t *model[2] = { ma, mb };
    unsigned full = 0, none = 0, part = 0;
    for (int step = 0; step < 6000; step++) {
        unsigned which = rnd(2), op = rnd(8);
        /* small boxes mostly, so that a texture often becomes whole or empty; now and then all of it */
        uint32_t x0 = rnd(W), y0 = rnd(H), w = 1 + rnd(W - x0), h = 1 + rnd(H - y0);
        if (rnd(40) == 0) { x0 = y0 = 0; w = W; h = H; }
        D3D11_BOX box = { x0, y0, 0, x0 + w, y0 + h, 1 };
        if (op < 4) {                                   /* the game's bytes into a box */
            COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)tex[which], 0, &box, data, w * 2, 0);
            for (uint32_t y = y0; y < y0 + h; y++) memset(model[which] + y * W + x0, 1, w);
        } else if (op < 6) {                            /* a box from a texture nothing was put in */
            COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)tex[which], 0, x0, y0, 0, (ID3D11Resource *)empty, 0, &box);
            for (uint32_t y = y0; y < y0 + h; y++) memset(model[which] + y * W + x0, 0, w);
        } else {                                        /* a box of the other texture, to another place */
            uint32_t dx = rnd(W - w + 1), dy = rnd(H - h + 1);
            COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)tex[which], 0, dx, dy, 0, (ID3D11Resource *)tex[!which], 0, &box);
            for (uint32_t y = 0; y < h; y++) memcpy(model[which] + (dy + y) * W + dx, model[!which] + (y0 + y) * W + x0, w);
        }
        check(a, ma, step);
        check(b, mb, step);
        unsigned kind = rt_nulldev_content(tex[which], 0);
        full += kind == ND_CONTENT_GAME; none += kind == ND_CONTENT_NONE; part += kind == ND_CONTENT_PART;
    }
    T_CASE("the record agrees with a flag per unit after each of 6000 boxes, and all three kinds were met");
    T_TRUE(full > 20);
    T_TRUE(none > 20);
    T_TRUE(part > 1000);
    check(a, ma, 6000);

    T_CASE("a read mapping is taken exactly when every unit is the game's");
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_BOX one = { 5, 3, 0, 6, 4, 1 };
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)a, 0, NULL, data, W * 2, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)a, 0, D3D11_MAP_READ, 0, &m), S_OK);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)a, 0);
    COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)a, 0, 5, 3, 0, (ID3D11Resource *)empty, 0, &one);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)a, 0, D3D11_MAP_READ, 0, &m), "(subresource 0: 90 of 91 units");
    COM_CALL(ID3D11DeviceContext, UpdateSubresource, context, (ID3D11Resource *)a, 0, &one, data, 2, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)a, 0, D3D11_MAP_READ, 0, &m), S_OK);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)a, 0);

    T_CASE("the records are counted and freed with their resources");
    NdTotals before, after;
    COM_CALL(ID3D11DeviceContext, CopySubresourceRegion, context, (ID3D11Resource *)a, 0, 5, 3, 0, (ID3D11Resource *)empty, 0, &one);
    T_TRUE(rt_nulldev_block(a, 0)->written != NULL);
    rt_nulldev_totals(&before);
    T_EQ(rt_com_release(a), 0);
    rt_nulldev_totals(&after);
    T_EQ(after.masks, before.masks - 1);
    T_TRUE(after.masks_made >= 3);
}
