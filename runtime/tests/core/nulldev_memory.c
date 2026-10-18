/* The do-nothing device's format table and level arithmetic (runtime/nulldev/memory.c), against
 * numbers worked by hand from Microsoft's pages, not from the code (docs/handoff/
 * d3d-resources.md, section 2, shows the working):
 *   DXGI_FORMAT's page states each format's size in bits; "Texture Block Compression in
 *   Direct3D 11" gives 8 bytes per 4 by 4 block for BC1 and BC4 and 16 for BC2, BC3, BC5, BC6H
 *   and BC7; the DDS programming guide gives a row of blocks as max(1, (width + 3) / 4) *
 *   block-size and of other formats as (width * bits + 7) / 8; each mip level is half the
 *   previous one, rounded down, minimum 1 (D3D11_SUBRESOURCE_DATA; CreateTexture2D's and
 *   CreateTexture3D's examples). */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(core, nulldev_memory)
{
    uint32_t bytes, block;
    NdLevel l;

    T_CASE("bytes per element are the format's stated bits over 8");
    static const struct { uint32_t format, bytes; } plain[] = {
        { 2, 16 },      /* R32G32B32A32_FLOAT: "128-bit" */     { 6, 12 },      /* R32G32B32_FLOAT: 96 */
        { 10, 8 },      /* R16G16B16A16_FLOAT: 64 */            { 16, 8 },      /* R32G32_FLOAT: 64 */
        { 20, 8 },      /* D32_FLOAT_S8X24_UINT: 32 and "an additional 32 bits" */
        { 24, 4 },      /* R10G10B10A2_UNORM: 32 */             { 26, 4 },      /* R11G11B10_FLOAT: 32 */
        { 28, 4 },      /* R8G8B8A8_UNORM: 32 */                { 29, 4 },      /* R8G8B8A8_UNORM_SRGB: 32 */
        { 34, 4 },      /* R16G16_FLOAT: 32 */                  { 40, 4 },      /* D32_FLOAT: 32 */
        { 41, 4 },      /* R32_FLOAT: 32 */                     { 45, 4 },      /* D24_UNORM_S8_UINT: 32 */
        { 49, 2 },      /* R8G8_UNORM: 16 */                    { 54, 2 },      /* R16_FLOAT: 16 */
        { 55, 2 },      /* D16_UNORM: 16 */                     { 56, 2 },      /* R16_UNORM: 16 */
        { 61, 1 },      /* R8_UNORM: 8 */                       { 65, 1 },      /* A8_UNORM: 8 */
        { 67, 4 },      /* R9G9B9E5_SHAREDEXP: 32 */            { 85, 2 },      /* B5G6R5_UNORM: 16 */
        { 86, 2 },      /* B5G5R5A1_UNORM: 16 */                { 87, 4 },      /* B8G8R8A8_UNORM: 32 */
        { 88, 4 },      /* B8G8R8X8_UNORM: 32 */                { 91, 4 },      /* B8G8R8A8_UNORM_SRGB: 32 */
        { 115, 2 },     /* B4G4R4A4_UNORM: 16 */                { 1, 16 },      { 4, 16 }, { 5, 12 }, { 8, 12 }, { 9, 8 },
        { 22, 8 }, { 23, 4 }, { 47, 4 }, { 48, 2 }, { 59, 2 }, { 60, 1 }, { 93, 4 },    /* the ends of each run of equal size */
    };
    for (unsigned i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        bytes = block = 0;
        T_TRUE(rt_nulldev_format_bytes(plain[i].format, &bytes, &block));
        T_EQ(bytes, plain[i].bytes);
        T_EQ(block, 1);
    }

    T_CASE("the block-compressed formats: 8 bytes a block for BC1 and BC4, 16 for the others");
    static const struct { uint32_t first, last, bytes; } bc[] = {
        { 70, 72, 8 }, { 73, 75, 16 }, { 76, 78, 16 }, { 79, 81, 8 }, { 82, 84, 16 }, { 94, 96, 16 }, { 97, 99, 16 },
    };
    for (unsigned i = 0; i < sizeof bc / sizeof bc[0]; i++)
        for (uint32_t f = bc[i].first; f <= bc[i].last; f++) {
            bytes = block = 0;
            T_TRUE(rt_nulldev_format_bytes(f, &bytes, &block));
            T_EQ(bytes, bc[i].bytes);
            T_EQ(block, 4);
        }

    T_CASE("no size: UNKNOWN, R1, the two 4:2:2 formats, the video formats, a number that is no format");
    static const uint32_t none[] = { 0, 66, 68, 69, 100, 103, 107, 114, 116, 0xffffffffu };
    for (unsigned i = 0; i < sizeof none / sizeof none[0]; i++) T_TRUE(!rt_nulldev_format_bytes(none[i], &bytes, &block));
    T_TRUE(!rt_nulldev_level(103, 16, 16, 1, 0, &l));

    T_CASE("a full chain goes down to 1 by 1 by 1");
    T_EQ(rt_nulldev_full_mips(32, 32, 1), 6);           /* CreateTexture2D's example */
    T_EQ(rt_nulldev_full_mips(32, 32, 4), 6);           /* CreateTexture3D's example */
    T_EQ(rt_nulldev_full_mips(512, 512, 1), 10);        /* Wine's test_create_texture2d (the contract's check) */
    T_EQ(rt_nulldev_full_mips(1, 1, 1), 1);
    T_EQ(rt_nulldev_full_mips(1920, 1080, 1), 11);      /* 1920, 960, 480, 240, 120, 60, 30, 15, 7, 3, 1 */
    T_EQ(rt_nulldev_full_mips(5, 300, 1), 9);           /* 300, 150, 75, 37, 18, 9, 4, 2, 1 */

    T_CASE("levels of 1920 by 1080 R8G8B8A8: each half the one before, rounded down");
    static const uint32_t w[11] = { 1920, 960, 480, 240, 120, 60, 30, 15, 7, 3, 1 }, h[11] = { 1080, 540, 270, 135, 67, 33, 16, 8, 4, 2, 1 };
    for (UINT i = 0; i < 11; i++) {
        T_TRUE(rt_nulldev_level(28, 1920, 1080, 1, i, &l));
        T_EQ(l.width, w[i]);
        T_EQ(l.height, h[i]);
        T_EQ(l.depth, 1);
        T_EQ(l.row_bytes, w[i] * 4);
        T_EQ(l.rows, h[i]);
        T_EQ(l.bytes, (uint64_t)w[i] * 4 * h[i]);
    }
    T_TRUE(rt_nulldev_level(28, 1920, 1080, 1, 0, &l));
    T_EQ(l.bytes, 8294400);                             /* 1920 * 1080 * 4 */

    T_CASE("block rows and columns round up: BC3 of 30 by 15 is 8 by 4 blocks");
    T_TRUE(rt_nulldev_level(77, 30, 15, 1, 0, &l));
    T_EQ(l.row_bytes, 128);                             /* (30 + 3) / 4 = 8 blocks of 16 bytes */
    T_EQ(l.rows, 4);                                    /* (15 + 3) / 4 */
    T_EQ(l.bytes, 512);
    T_TRUE(rt_nulldev_level(77, 30, 15, 1, 4, &l));     /* 1 by 1 (30, 15, 7, 3, 1): one whole block */
    T_EQ(l.width, 1);
    T_EQ(l.bytes, 16);

    T_CASE("a 3D level has all its slices: 64 by 64 by 64 R16G16B16A16, level 2");
    T_TRUE(rt_nulldev_level(10, 64, 64, 64, 2, &l));    /* 16 by 16 by 16, 8 bytes an element */
    T_EQ(l.row_bytes, 128);
    T_EQ(l.slice_bytes, 2048);
    T_EQ(l.bytes, 32768);
}
