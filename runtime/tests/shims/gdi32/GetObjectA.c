#include <string.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core's record: each object is made by the core's own creators, and what
 * GetObjectA writes is compared member by member with the value the object was made from.
 * A NULL buffer gives the size of the type's first structure; a buffer one byte short gives
 * 0 and nothing is written into it. */

static int all_bytes(const void *p, size_t n, unsigned char v)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i] != v) return 0;
    return 1;
}

TEST(gdi32, GetObjectA)
{
    HPEN pen;
    HBRUSH brush;
    HBITMAP bmp, dib;
    HFONT font;
    HRGN rgn;
    LOGPEN lp;
    LOGBRUSH lb;
    LOGFONTA lfa, lf;
    BITMAP bm;
    DIBSECTION ds;
    BITMAPINFO bmi;
    HDC dc;
    void *bits = NULL;
    unsigned char buf[128];

    T_CASE("pen record of 16 bytes");
    pen = rt_gdi_create_pen(0, -123, 0x00030201u);
    T_REQUIRE(pen != NULL);
    memset(&lp, 0xAA, sizeof lp);
    T_EQ(CALL(gdi32, GetObjectA, pen, 16, &lp), 16);
    T_EQ(lp.lopnStyle, 0);
    T_EQ(lp.lopnWidth.x, 123);
    T_EQ(lp.lopnWidth.y, 0);
    T_EQ(lp.lopnColor, 0x00030201u);

    T_CASE("brush record of 16 bytes");
    brush = rt_gdi_create_brush(0x00030201u);
    T_REQUIRE(brush != NULL);
    memset(&lb, 0xAA, sizeof lb);
    T_EQ(CALL(gdi32, GetObjectA, brush, 16, &lb), 16);
    T_EQ(lb.lbStyle, BS_SOLID);
    T_EQ(lb.lbColor, 0x00030201u);
    T_EQ(lb.lbHatch, 0);

    T_CASE("font record of 60 bytes as LOGFONTA");
    memset(&lfa, 0, sizeof lfa);
    lfa.lfHeight = -12;
    lfa.lfWeight = 700;
    strcpy(lfa.lfFaceName, "Arial");
    font = rt_gdi_create_font_a(&lfa);
    T_REQUIRE(font != NULL);
    memset(&lf, 0xAA, sizeof lf);
    T_EQ(CALL(gdi32, GetObjectA, font, 60, &lf), 60);
    T_EQ(lf.lfHeight, -12);
    T_EQ(lf.lfWeight, 700);
    T_TRUE(strcmp(lf.lfFaceName, "Arial") == 0);

    T_CASE("bitmap record of 32 bytes");
    bmp = rt_gdi_create_bitmap(5, 8, 1, 1, NULL);
    T_REQUIRE(bmp != NULL);
    memset(&bm, 0xAA, sizeof bm);
    T_EQ(CALL(gdi32, GetObjectA, bmp, 32, &bm), 32);
    T_EQ(bm.bmType, 0);
    T_EQ(bm.bmWidth, 5);
    T_EQ(bm.bmHeight, 8);
    T_EQ(bm.bmWidthBytes, 2);                   /* one byte of bits, rounded up to two */
    T_EQ(bm.bmPlanes, 1);
    T_EQ(bm.bmBitsPixel, 1);
    T_TRUE(bm.bmBits == NULL);

    T_CASE("DIB section record of 104 bytes");
    dc = rt_dc_create_compatible(NULL);
    T_REQUIRE(dc != NULL);
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = 40;
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dib = rt_gdi_create_dib_section(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_REQUIRE(dib != NULL);
    memset(&ds, 0xAA, sizeof ds);
    T_EQ(CALL(gdi32, GetObjectA, dib, 104, &ds), 104);
    T_EQ(ds.dsBmih.biHeight, 16);               /* the absolute value, though top-down */
    T_EQ(ds.dsBm.bmHeight, 16);
    T_TRUE(ds.dshSection == NULL);
    T_EQ(ds.dsOffset, 0);

    T_CASE("NULL buffer returns the size");
    T_EQ(CALL(gdi32, GetObjectA, pen, 16, NULL), 16);
    T_EQ(CALL(gdi32, GetObjectA, brush, 16, NULL), 16);
    T_EQ(CALL(gdi32, GetObjectA, font, 60, NULL), 60);
    T_EQ(CALL(gdi32, GetObjectA, bmp, 32, NULL), 32);
    T_EQ(CALL(gdi32, GetObjectA, dib, 0, NULL), 32);   /* 32 for a DIB section too, not 104 */

    T_CASE("buffer one byte short returns 0 and is not written");
    memset(buf, 0xAA, sizeof buf);
    T_EQ(CALL(gdi32, GetObjectA, pen, 15, buf), 0);
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));
    T_EQ(CALL(gdi32, GetObjectA, brush, 15, buf), 0);
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));
    T_EQ(CALL(gdi32, GetObjectA, font, 59, buf), 0);
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));
    T_EQ(CALL(gdi32, GetObjectA, bmp, 31, buf), 0);
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));

    T_CASE("handle that is no object returns 0");
    T_EQ(CALL(gdi32, GetObjectA, NULL, 16, buf), 0);
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));
    T_EQ(rt_gdi_delete(pen), TRUE);
    T_EQ(CALL(gdi32, GetObjectA, pen, 16, buf), 0);     /* the record is gone */
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));
    rgn = rt_gdi_create_rect_rgn(0, 0, 10, 10);
    T_REQUIRE(rgn != NULL);
    T_EQ(CALL(gdi32, GetObjectA, rgn, 16, buf), 0);     /* a region is not an object here */
    T_TRUE(all_bytes(buf, sizeof buf, 0xAA));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, GetObjectA, brush, 16, &lb), 16);
    T_EQ(T_LAST_ERROR(), 0x1234);               /* success leaves last error alone */
    T_EQ(CALL(gdi32, GetObjectA, NULL, 16, buf), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);               /* the core sets none for a handle that is no object */
}
