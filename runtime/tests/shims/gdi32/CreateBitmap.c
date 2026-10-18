#include "rt/test.h"
#include "rt/gdi.h"

/* The oracle is the core's record, read back with GetObject as the guest would: the size,
 * format and row bytes (rounded up to 2 bytes) of the bitmap, and bmBits NULL for a bitmap
 * with no pixel storage. */
TEST(gdi32, CreateBitmap)
{
    HBITMAP h;
    BITMAP bm;
    DWORD bits[16] = {0};
    unsigned bpp;

    T_CASE("creates a bitmap of the given size and format with no pixel storage");
    h = CALL(gdi32, CreateBitmap, 64, 32, 1, 32, NULL);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmType, 0);
    T_EQ(bm.bmWidth, 64);
    T_EQ(bm.bmHeight, 32);
    T_EQ(bm.bmWidthBytes, 256);
    T_EQ(bm.bmPlanes, 1);
    T_EQ(bm.bmBitsPixel, 32);
    T_TRUE(bm.bmBits == NULL);

    T_CASE("formats of 1, 8, 16, 24 and 32 bits per pixel");
    for (bpp = 1; bpp <= 32; bpp++) {
        if (bpp != 1 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) continue;
        h = CALL(gdi32, CreateBitmap, 10, 4, 1, bpp, NULL);
        T_TRUE(h != NULL);
        T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
        T_EQ(bm.bmWidth, 10);
        T_EQ(bm.bmHeight, 4);
        T_EQ(bm.bmBitsPixel, bpp);
        T_EQ(bm.bmWidthBytes, ((10 * bpp + 15) >> 4) << 1);
        T_TRUE(bm.bmBits == NULL);
    }

    T_CASE("a width or height of 0 gives a 1 x 1 monochrome bitmap whatever the planes and bits per pixel");
    h = CALL(gdi32, CreateBitmap, 0, 0, 0, 0, NULL);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 1);
    T_EQ(bm.bmHeight, 1);
    T_EQ(bm.bmBitsPixel, 1);
    h = CALL(gdi32, CreateBitmap, 0, 5, 2, 7, NULL);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 1);
    T_EQ(bm.bmHeight, 1);
    T_EQ(bm.bmBitsPixel, 1);
    h = CALL(gdi32, CreateBitmap, 5, 0, 1, 24, NULL);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 1);
    T_EQ(bm.bmHeight, 1);
    T_EQ(bm.bmBitsPixel, 1);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(gdi32, CreateBitmap, 8, 8, 1, 32, NULL);
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(gdi32, CreateBitmap, 2, 2, 1, 32, bits), "rt_gdi_create_bitmap: not implemented");
    T_TRAPS(CALL(gdi32, CreateBitmap, -1, 2, 1, 32, NULL), "rt_gdi_create_bitmap: not implemented");
    T_TRAPS(CALL(gdi32, CreateBitmap, 2, -1, 1, 32, NULL), "rt_gdi_create_bitmap: not implemented");
    T_TRAPS(CALL(gdi32, CreateBitmap, 2, 2, 2, 32, NULL), "rt_gdi_create_bitmap: not implemented");
    T_TRAPS(CALL(gdi32, CreateBitmap, 2, 2, 1, 2, NULL), "rt_gdi_create_bitmap: not implemented");
}
