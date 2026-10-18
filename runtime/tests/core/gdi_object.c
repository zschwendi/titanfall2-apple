/* The piece gdi-object (rt/gdi.h, core/gdi_object.c): pens, brushes, fonts, bitmaps and
 * regions as records.  Expected values: the comments of rt/gdi.h, which name their source
 * (Wine gdi32/tests/pen.c, brush.c, font.c, bitmap.c, dib.c, gdiobj.c; reviews D15, D16,
 * D18, S28, V47 to V53, Z3). */
#include "rt/test.h"
#include "../../core/win_priv.h"

TEST(core, gdi_object)
{
    HDC s = rt_dc_get(NULL);
    HDC m = rt_dc_create_compatible(NULL);
    HPEN p = NULL;
    HBRUSH b = NULL;
    HFONT f = NULL;
    unsigned char buf[104];

    T_PLAN(1, "a pen: style, width, colour") {
        rt_thread_set_last_error(0xDEAD);
        p = rt_gdi_create_pen(0, 3, 0x112233);
        T_TRUE(p != NULL && (uintptr_t)p >= 0x40000000);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        LOGPEN lp;
        T_EQ(rt_gdi_get_object(p, 16, &lp, false), 16);
        T_EQ(lp.lopnStyle, 0);
        T_EQ(lp.lopnWidth.x, 3);
        T_EQ(lp.lopnWidth.y, 0);
        T_EQ(lp.lopnColor, 0x112233);
    }

    T_PLAN(2, "pen width is absolute; the null pen reads back as PS_NULL") {
        LOGPEN lp;
        HPEN q = rt_gdi_create_pen(2, -123, 0);
        T_EQ(rt_gdi_get_object(q, 16, &lp, false), 16);
        T_EQ(lp.lopnWidth.x, 123);
        q = rt_gdi_create_pen(5, 7, 0x445566);
        T_EQ(rt_gdi_get_object(q, 16, &lp, false), 16);
        T_EQ(lp.lopnStyle, 5);
        T_EQ(lp.lopnWidth.x, 1);
        T_EQ(lp.lopnWidth.y, 0);
        T_EQ(lp.lopnColor, 0);
    }

    T_PLAN(3, "a pen style outside 0 to 6 stops") {
        T_TRAPS(rt_gdi_create_pen(7, 1, 0), "rt_gdi_create_pen");
        T_TRAPS(rt_gdi_create_pen(-1, 1, 0), "rt_gdi_create_pen");
    }

    T_PLAN(4, "a solid brush") {
        b = rt_gdi_create_brush(0x00FF00);
        T_TRUE(b != NULL);
        LOGBRUSH lb;
        T_EQ(rt_gdi_get_object(b, 16, &lb, false), 16);
        T_EQ(lb.lbStyle, BS_SOLID);
        T_EQ(lb.lbColor, 0x00FF00);
        T_EQ(lb.lbHatch, 0);
    }

    T_PLAN(5, "a font: LOGFONTA and LOGFONTW read back") {
        LOGFONTA lf = { .lfHeight = -12, .lfWeight = 700, .lfFaceName = "Tahoma" };
        f = rt_gdi_create_font_a(&lf);
        T_TRUE(f != NULL);
        LOGFONTA out;
        T_EQ(rt_gdi_get_object(f, 60, &out, false), 60);
        T_EQ(memcmp(&out, &lf, 60), 0);
        LOGFONTW ow;
        T_EQ(rt_gdi_get_object(f, 92, &ow, true), 92);
        T_EQ(ow.lfHeight, -12);
        T_EQ(ow.lfWeight, 700);
        T_EQ(ow.lfFaceName[0], u'T');
        T_EQ(ow.lfFaceName[5], u'a');
        T_EQ(ow.lfFaceName[6], 0);
    }

    T_PLAN(6, "no buffer: the size of the first structure, whatever cb is") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object(p, 0, NULL, false), 16);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object(b, 0, NULL, false), 16);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object(f, 0, NULL, false), 60);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object(f, 0, NULL, true), 92);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(7, "a cb below the structure's size writes nothing") {
        unsigned char want[104];
        memset(want, 0xA5, sizeof want);
        memset(buf, 0xA5, sizeof buf);
        T_EQ(rt_gdi_get_object(p, 15, buf, false), 0);
        T_TRUE(memcmp(buf, want, sizeof buf) == 0);
        T_EQ(rt_gdi_get_object(b, 15, buf, false), 0);
        T_TRUE(memcmp(buf, want, sizeof buf) == 0);
        T_EQ(rt_gdi_get_object(f, 59, buf, false), 0);
        T_TRUE(memcmp(buf, want, sizeof buf) == 0);
    }

    T_PLAN(8, "a handle that is not an object, and a device context") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object(NULL, 16, buf, false), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object((HANDLE)0x40000FF0, 16, buf, false), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_get_object((HANDLE)s, 16, buf, false), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(21, "delete a pen: TRUE once, then FALSE for every other handle") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_delete(p), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_gdi_get_object(p, 16, buf, false), 0);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_delete(p), FALSE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_delete(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_delete((HGDIOBJ)0x40000FF0), FALSE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(22, "a brush selected into a context cannot be deleted") {
        rt_dc_select(m, b);
        T_TRAPS(rt_gdi_delete(b), "rt_gdi_delete");
        rt_dc_select(m, NULL);
        T_EQ(rt_gdi_delete(b), TRUE);
    }
}

TEST(core, gdi_object_bitmap)
{
    HDC s = rt_dc_get(NULL);
    HDC m = rt_dc_create_compatible(NULL);

    T_PLAN(9, "a 4 x 3 32-bit bitmap record, no pixel storage") {
        HBITMAP x = rt_gdi_create_bitmap(4, 3, 1, 32, NULL);
        T_TRUE(x != NULL);
        BITMAP bm;
        T_EQ(rt_gdi_get_object(x, 32, &bm, false), 32);
        T_EQ(bm.bmType, 0);
        T_EQ(bm.bmWidth, 4);
        T_EQ(bm.bmHeight, 3);
        T_EQ(bm.bmWidthBytes, 16);
        T_EQ(bm.bmPlanes, 1);
        T_EQ(bm.bmBitsPixel, 32);
        T_TRUE(bm.bmBits == NULL);
        T_EQ(rt_gdi_get_object(x, 0, NULL, false), 32);
        T_EQ(rt_gdi_get_object(x, 31, &bm, false), 0);
    }

    T_PLAN(10, "row bytes rounded up to 2 bytes") {
        BITMAP bm;
        T_EQ(rt_gdi_get_object(rt_gdi_create_bitmap(5, 2, 1, 8, NULL), 32, &bm, false), 32);
        T_EQ(bm.bmWidthBytes, 6);
        T_EQ(rt_gdi_get_object(rt_gdi_create_bitmap(9, 1, 1, 1, NULL), 32, &bm, false), 32);
        T_EQ(bm.bmWidthBytes, 2);
    }

    T_PLAN(11, "a zero size gives a 1 x 1 monochrome bitmap") {
        BITMAP bm;
        T_EQ(rt_gdi_get_object(rt_gdi_create_bitmap(0, 5, 0, 0, NULL), 32, &bm, false), 32);
        T_EQ(bm.bmWidth, 1);
        T_EQ(bm.bmHeight, 1);
        T_EQ(bm.bmBitsPixel, 1);
        T_EQ(bm.bmWidthBytes, 2);
        T_EQ(rt_gdi_get_object(rt_gdi_create_bitmap(5, 0, 1, 32, NULL), 32, &bm, false), 32);
        T_EQ(bm.bmWidth, 1);
        T_EQ(bm.bmHeight, 1);
        T_EQ(bm.bmBitsPixel, 1);
        T_EQ(bm.bmWidthBytes, 2);
    }

    T_PLAN(12, "bits given, a negative size, other planes or bpp stop") {
        unsigned char px[4];
        T_TRAPS(rt_gdi_create_bitmap(4, 4, 1, 32, &px), "rt_gdi_create_bitmap");
        T_TRAPS(rt_gdi_create_bitmap(-1, 4, 1, 32, NULL), "rt_gdi_create_bitmap");
        T_TRAPS(rt_gdi_create_bitmap(4, 4, 2, 32, NULL), "rt_gdi_create_bitmap");
        T_TRAPS(rt_gdi_create_bitmap(4, 4, 1, 4, NULL), "rt_gdi_create_bitmap");
    }

    T_PLAN(13, "a compatible bitmap has the format of the context") {
        BITMAP bm;
        T_EQ(rt_gdi_get_object(rt_gdi_create_compatible_bitmap(s, 10, 20), 32, &bm, false), 32);
        T_EQ(bm.bmWidth, 10);
        T_EQ(bm.bmHeight, 20);
        T_EQ(bm.bmBitsPixel, 32);
        T_EQ(rt_gdi_get_object(rt_gdi_create_compatible_bitmap(m, 10, 20), 32, &bm, false), 32);
        T_EQ(bm.bmWidth, 10);
        T_EQ(bm.bmHeight, 20);
        T_EQ(bm.bmBitsPixel, 1);
        HBITMAP x = rt_gdi_create_bitmap(4, 3, 1, 32, NULL);
        rt_dc_select(m, x);
        T_EQ(rt_gdi_get_object(rt_gdi_create_compatible_bitmap(m, 2, 2), 32, &bm, false), 32);
        T_EQ(bm.bmBitsPixel, 32);
        T_EQ(rt_gdi_get_object(rt_gdi_create_compatible_bitmap(s, 0, 0), 32, &bm, false), 32);
        T_EQ(bm.bmWidth, 1);
        T_EQ(bm.bmHeight, 1);
        T_EQ(bm.bmBitsPixel, 1);
    }

    T_PLAN(14, "a compatible bitmap of a handle that is no context") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_gdi_create_compatible_bitmap((HDC)0x40000FF0, 1, 1) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }
}

TEST(core, gdi_object_dib)
{
    (void)rt_dc_get(NULL);
    (void)rt_dc_create_compatible(NULL);
    BITMAPINFO bi = { .bmiHeader = { .biSize = 40, .biWidth = 16, .biHeight = -16, .biPlanes = 1, .biBitCount = 32 } };
    void *bits = (void *)1;
    HBITMAP d = NULL;

    T_PLAN(15, "a DIB section: zero-filled guest memory, the address in *bits") {
        rt_thread_set_last_error(0xDEAD);
        d = rt_gdi_create_dib_section(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        T_TRUE(d != NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(bits != NULL && bits != (void *)1);
        uint64_t a = (uint64_t)(uintptr_t)bits;
        T_EQ(LD8(a), 0);
        T_EQ(LD8(a + 1023), 0);
        ST8(a, 0x5A);
        ST8(a + 1023, 0x5A);
        T_EQ(LD8(a), 0x5A);
        T_EQ(LD8(a + 1023), 0x5A);
    }

    T_PLAN(16, "the DIBSECTION of a top-down 16 x 16 section") {
        DIBSECTION ds;
        T_EQ(rt_gdi_get_object(d, 104, &ds, true), 104);
        T_EQ(ds.dsBmih.biHeight, 16);
        T_EQ(ds.dsBm.bmHeight, 16);
        T_EQ(ds.dsBm.bmWidth, 16);
        T_EQ(ds.dsBm.bmWidthBytes, 64);
        T_EQ(ds.dsBm.bmBitsPixel, 32);
        T_TRUE((uintptr_t)ds.dsBm.bmBits == (uintptr_t)bits);
        T_EQ(ds.dsBmih.biCompression, BI_RGB);
        T_EQ(ds.dsBitfields[0], 0);
        T_EQ(ds.dsBitfields[1], 0);
        T_EQ(ds.dsBitfields[2], 0);
        T_TRUE(ds.dshSection == NULL);
        T_EQ(ds.dsOffset, 0);
        T_EQ(rt_gdi_get_object(d, 0, NULL, true), 32);
        T_EQ(rt_gdi_get_object(d, 32, &ds, false), 32);
    }

    T_PLAN(17, "a 16-bit BI_RGB section reports the 5-5-5 masks") {
        BITMAPINFO bi16 = { .bmiHeader = { .biSize = 40, .biWidth = 4, .biHeight = 4, .biPlanes = 1, .biBitCount = 16 } };
        void *bits16 = NULL;
        HBITMAP d16 = rt_gdi_create_dib_section(NULL, &bi16, DIB_RGB_COLORS, &bits16, NULL, 0);
        T_TRUE(d16 != NULL);
        DIBSECTION ds16;
        T_EQ(rt_gdi_get_object(d16, 104, &ds16, true), 104);
        T_EQ(ds16.dsBmih.biCompression, 3);
        T_EQ(ds16.dsBitfields[0], 0x7C00);
        T_EQ(ds16.dsBitfields[1], 0x3E0);
        T_EQ(ds16.dsBitfields[2], 0x1F);
        T_EQ(ds16.dsBm.bmWidthBytes, 8);
    }

    T_PLAN(18, "a bad header, or a zero or negative size: NULL, ERROR_INVALID_PARAMETER, *bits NULL") {
        void *q;
        BITMAPINFO b;
        HBITMAP r;
        rt_thread_set_last_error(0xDEAD);
        q = (void *)1;
        r = rt_gdi_create_dib_section(NULL, NULL, DIB_RGB_COLORS, &q, NULL, 0);
        T_TRUE(r == NULL && q == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

        b = bi; b.bmiHeader.biSize = 39;
        rt_thread_set_last_error(0xDEAD);
        q = (void *)1;
        r = rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0);
        T_TRUE(r == NULL && q == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

        b = bi; b.bmiHeader.biSize = 13;
        rt_thread_set_last_error(0xDEAD);
        q = (void *)1;
        r = rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0);
        T_TRUE(r == NULL && q == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

        b = bi; b.bmiHeader.biWidth = 0;
        rt_thread_set_last_error(0xDEAD);
        q = (void *)1;
        r = rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0);
        T_TRUE(r == NULL && q == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

        b = bi; b.bmiHeader.biWidth = -4;
        rt_thread_set_last_error(0xDEAD);
        q = (void *)1;
        r = rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0);
        T_TRUE(r == NULL && q == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

        b = bi; b.bmiHeader.biHeight = 0;
        rt_thread_set_last_error(0xDEAD);
        q = (void *)1;
        r = rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0);
        T_TRUE(r == NULL && q == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }

    T_PLAN(19, "a DIB section the core does not build stops") {
        void *q = (void *)1;
        BITMAPINFO b;
        b = bi; b.bmiHeader.biSize = 12;
        T_TRAPS(rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0), "rt_gdi_create_dib_section");
        T_TRAPS(rt_gdi_create_dib_section(NULL, &bi, DIB_RGB_COLORS, &q, (HANDLE)4, 0), "rt_gdi_create_dib_section");
        T_TRAPS(rt_gdi_create_dib_section(NULL, &bi, 1, &q, NULL, 0), "rt_gdi_create_dib_section");
        b = bi; b.bmiHeader.biPlanes = 2;
        T_TRAPS(rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0), "rt_gdi_create_dib_section");
        b = bi; b.bmiHeader.biBitCount = 4;
        T_TRAPS(rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0), "rt_gdi_create_dib_section");
        b = bi; b.bmiHeader.biCompression = 1;
        T_TRAPS(rt_gdi_create_dib_section(NULL, &b, DIB_RGB_COLORS, &q, NULL, 0), "rt_gdi_create_dib_section");
    }

    T_PLAN(23, "delete a DIB section: TRUE, and its record is gone") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_gdi_delete(d), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_gdi_get_object(d, 0, NULL, true), 0);
    }
}

TEST(core, gdi_object_region)
{
    (void)rt_dc_get(NULL);
    (void)rt_dc_create_compatible(NULL);
    unsigned char buf[104] = { 0 };

    T_PLAN(20, "a region record; SetRectRgn is TRUE for a region and FALSE for no region") {
        HRGN g = rt_gdi_create_rect_rgn(1, 2, 3, 4);
        T_TRUE(g != NULL);
        T_EQ(rt_gdi_get_object(g, 16, buf, false), 0);
        T_EQ(rt_gdi_set_rect_rgn(g, 5, 6, 7, 8), TRUE);
        HPEN pen = rt_gdi_create_pen(0, 1, 0);
        T_EQ(rt_gdi_set_rect_rgn((HRGN)pen, 0, 0, 1, 1), FALSE);
        T_EQ(rt_gdi_set_rect_rgn(NULL, 0, 0, 1, 1), FALSE);
    }
}
