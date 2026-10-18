#include "rt/test.h"
#include "rt/gdi.h"

/* The oracle is the core's record, read back with GetObject as the guest would: the size and
 * format of the bitmap, which follow the context it was made for (the screen's format for a
 * display context, the format of the bitmap selected into a memory context, else 1 bit). */
TEST(gdi32, CreateCompatibleBitmap)
{
    HDC screen, mem;
    HBITMAP h, bmp;
    BITMAP bm;

    screen = rt_dc_get(NULL);
    T_REQUIRE(screen != NULL);
    mem = rt_dc_create_compatible(NULL);
    T_REQUIRE(mem != NULL);

    T_CASE("a display context gives a bitmap of the screen's format (32 bits)");
    h = CALL(gdi32, CreateCompatibleBitmap, screen, 64, 32);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 64);
    T_EQ(bm.bmHeight, 32);
    T_EQ(bm.bmPlanes, 1);
    T_EQ(bm.bmBitsPixel, 32);
    T_EQ(bm.bmWidthBytes, 256);
    T_TRUE(bm.bmBits == NULL);

    T_CASE("a memory context gives a monochrome bitmap, or the format of the bitmap selected into it");
    h = CALL(gdi32, CreateCompatibleBitmap, mem, 4, 4);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 4);
    T_EQ(bm.bmHeight, 4);
    T_EQ(bm.bmBitsPixel, 1);
    bmp = CALL(gdi32, CreateCompatibleBitmap, screen, 16, 8);
    T_TRUE(bmp != NULL);
    rt_dc_select(mem, (HGDIOBJ)bmp);
    h = CALL(gdi32, CreateCompatibleBitmap, mem, 4, 4);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 4);
    T_EQ(bm.bmHeight, 4);
    T_EQ(bm.bmBitsPixel, 32);

    T_CASE("a width or height of 0 gives a 1 x 1 monochrome bitmap");
    h = CALL(gdi32, CreateCompatibleBitmap, screen, 0, 0);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 1);
    T_EQ(bm.bmHeight, 1);
    T_EQ(bm.bmBitsPixel, 1);
    h = CALL(gdi32, CreateCompatibleBitmap, screen, 0, 5);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidth, 1);
    T_EQ(bm.bmHeight, 1);
    T_EQ(bm.bmBitsPixel, 1);

    T_CASE("a handle that is no context gives NULL");
    h = CALL(gdi32, CreateCompatibleBitmap, (HDC)bmp, 4, 4);
    T_TRUE(h == NULL);

    T_CASE("success and failure leave last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(gdi32, CreateCompatibleBitmap, screen, 8, 8);
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    h = CALL(gdi32, CreateCompatibleBitmap, (HDC)bmp, 4, 4);
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(gdi32, CreateCompatibleBitmap, screen, -1, 5), "rt_gdi_create_compatible_bitmap: not implemented");
    T_TRAPS(CALL(gdi32, CreateCompatibleBitmap, screen, 5, -1), "rt_gdi_create_compatible_bitmap: not implemented");
}
