#include <string.h>

#include "rt/test.h"
#include "rt/gdi.h"

/* The oracle is the core's record, read back with GetObject as the guest would, and the guest
 * memory the bits pointer names: the bytes are read and written through the pointer.  The
 * sizes are the documented row sizes, rounded up to 4 bytes for a DIB section. */
static void dib_header(BITMAPINFO *bmi, LONG w, LONG h, WORD bpp, DWORD comp)
{
    memset(bmi, 0, sizeof *bmi);
    bmi->bmiHeader.biSize = 40;
    bmi->bmiHeader.biWidth = w;
    bmi->bmiHeader.biHeight = h;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = bpp;
    bmi->bmiHeader.biCompression = comp;
}

TEST(gdi32, CreateDIBSection)
{
    BITMAPINFO bmi;
    BITMAP bm;
    DIBSECTION ds;
    HDC dc;
    HBITMAP h;
    void *bits;
    unsigned char *p;
    unsigned nonzero = 0, bad = 0;
    unsigned i;

    dc = CALL(gdi32, CreateCompatibleDC, NULL);
    T_TRUE(dc != NULL);

    T_CASE("creates a DIB section of zero-filled guest memory of the row size and height given");
    dib_header(&bmi, 100, -50, 32, 0);
    CALL(kernel32, SetLastError, 0x1234);
    bits = NULL;
    h = CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h != NULL);
    T_TRUE(bits != NULL);
    p = bits;
    for (i = 0; i < 20000; i++) if (p[i] != 0) nonzero++;
    T_EQ(nonzero, 0);
    for (i = 0; i < 20000; i++) p[i] = (unsigned char)(i * 7 + 1);
    for (i = 0; i < 20000; i++) if (p[i] != (unsigned char)(i * 7 + 1)) bad++;
    T_EQ(bad, 0);

    T_CASE("a top-down section reports its absolute height");
    T_EQ(rt_gdi_get_object(h, 104, &ds, false), 104);
    T_TRUE(ds.dsBm.bmBits == bits);
    T_EQ(ds.dsBm.bmWidth, 100);
    T_EQ(ds.dsBm.bmHeight, 50);
    T_EQ(ds.dsBm.bmWidthBytes, 400);
    T_EQ(ds.dsBm.bmBitsPixel, 32);
    T_EQ(ds.dsBmih.biWidth, 100);
    T_EQ(ds.dsBmih.biHeight, 50);
    T_EQ(ds.dsBmih.biBitCount, 32);
    T_EQ(rt_gdi_get_object(h, 0, NULL, true), 32);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmHeight, 50);

    T_CASE("success leaves last error alone");
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a 16-bit BI_RGB section reports BI_BITFIELDS and the 5-5-5 masks");
    dib_header(&bmi, 4, 4, 16, 0);
    bits = NULL;
    h = CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h != NULL);
    T_TRUE(bits != NULL);
    T_EQ(rt_gdi_get_object(h, 104, &ds, false), 104);
    T_EQ(ds.dsBmih.biCompression, 3);
    T_EQ(ds.dsBitfields[0], 0x7C00);
    T_EQ(ds.dsBitfields[1], 0x3E0);
    T_EQ(ds.dsBitfields[2], 0x1F);
    T_EQ(ds.dsBmih.biBitCount, 16);
    T_EQ(ds.dsBm.bmWidthBytes, 8);
    T_TRUE(ds.dshSection == NULL);
    T_EQ(ds.dsOffset, 0);
    T_EQ(rt_gdi_delete(h), TRUE);
    T_EQ(rt_gdi_get_object(h, 0, NULL, false), 0);

    T_CASE("a 24-bit row is rounded up to 4 bytes");
    dib_header(&bmi, 3, 1, 24, 0);
    bits = NULL;
    h = CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h != NULL);
    T_TRUE(bits != NULL);
    T_EQ(rt_gdi_get_object(h, 32, &bm, false), 32);
    T_EQ(bm.bmWidthBytes, 12);
    T_EQ(bm.bmHeight, 1);
    T_EQ(rt_gdi_delete(h), TRUE);

    T_CASE("NULL pbmi, a width of 0 and a biSize of 39 give NULL with ERROR_INVALID_PARAMETER");
    bits = (void *)&bmi;
    CALL(kernel32, SetLastError, 0);
    h = CALL(gdi32, CreateDIBSection, dc, NULL, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h == NULL);
    T_TRUE(bits == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    dib_header(&bmi, 0, 10, 32, 0);
    bits = (void *)&bmi;
    CALL(kernel32, SetLastError, 0);
    h = CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h == NULL);
    T_TRUE(bits == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    dib_header(&bmi, 10, 10, 32, 0);
    bmi.bmiHeader.biSize = 39;
    bits = (void *)&bmi;
    CALL(kernel32, SetLastError, 0);
    h = CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h == NULL);
    T_TRUE(bits == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("deleting a DIB section gives TRUE and the handle is no object");
    dib_header(&bmi, 100, -50, 32, 0);
    bits = NULL;
    h = CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    T_TRUE(h != NULL);
    T_EQ(rt_gdi_delete(h), TRUE);
    T_EQ(rt_gdi_get_object(h, 0, NULL, false), 0);

    /* The stops: the core stops the runtime on each of these, the shim adds no guard. */
    dib_header(&bmi, 10, 10, 32, 0);
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, (HANDLE)(uintptr_t)0x1234, 0), "rt_gdi_create_dib_section: not implemented");
    bmi.bmiHeader.biSize = 12;
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0), "rt_gdi_create_dib_section: not implemented");
    dib_header(&bmi, 10, 10, 32, 0);
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, 1u /* any usage other than DIB_RGB_COLORS (0) */, &bits, NULL, 0), "rt_gdi_create_dib_section: not implemented");
    dib_header(&bmi, 10, 10, 32, 0);
    bmi.bmiHeader.biPlanes = 2;
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0), "rt_gdi_create_dib_section: not implemented");
    dib_header(&bmi, 10, 10, 1, 0);
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0), "rt_gdi_create_dib_section: not implemented");
    dib_header(&bmi, 10, 10, 32, 1);
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0), "rt_gdi_create_dib_section: not implemented");
    dib_header(&bmi, 8193, 8193, 32, 0);
    T_TRAPS(CALL(gdi32, CreateDIBSection, dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0), "rt_gdi_create_dib_section: not implemented");
}
