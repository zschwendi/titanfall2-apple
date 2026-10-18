#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core's records: a context's selections are read back by selecting and
 * observing what comes out, and the objects are the core's own (rt_gdi_create_*).  Stock
 * defaults are the handle NULL (rt/gdi.h). */

#define NOT_A_CONTEXT ((HDC)(uintptr_t)0x40000FF0u)

TEST(gdi32, SelectObject)
{
    HDC mem, mem2, scr;
    HBRUSH b1, b2;
    HPEN p;
    HFONT f;
    HBITMAP bm;
    HRGN rgn;
    LOGFONTA lf;
    HGDIOBJ prev;

    mem = rt_dc_create_compatible(NULL);
    T_TRUE(mem != NULL);
    b1 = rt_gdi_create_brush(0x000011);
    b2 = rt_gdi_create_brush(0x000022);
    p = rt_gdi_create_pen(0, 1, 0x123456);
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -12;
    f = rt_gdi_create_font_a(&lf);

    T_CASE("selects a brush into a memory context, the first returns NULL and the next the first brush");
    T_EQ(CALL(gdi32, SelectObject, mem, b1), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, b2), (HGDIOBJ)b1);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)b2);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), NULL);

    T_CASE("a pen and a font have slots of their own");
    T_EQ(CALL(gdi32, SelectObject, mem, b1), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, p), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, f), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, p), (HGDIOBJ)p);
    T_EQ(CALL(gdi32, SelectObject, mem, f), (HGDIOBJ)f);
    T_EQ(CALL(gdi32, SelectObject, mem, b2), (HGDIOBJ)b1);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)f);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)p);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)b2);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), NULL);

    T_CASE("a bitmap into a display context is NULL");
    scr = rt_dc_create_display(u"DISPLAY", NULL);
    T_TRUE(scr != NULL);
    bm = rt_gdi_create_compatible_bitmap(mem, 4, 4);
    T_TRUE(bm != NULL);
    T_EQ(CALL(gdi32, SelectObject, scr, bm), NULL);
    T_EQ(rt_dc_delete(scr), TRUE);

    T_CASE("a handle that is no object is NULL");
    T_EQ(CALL(gdi32, SelectObject, mem, (HGDIOBJ)(uintptr_t)0x12345u), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, b1), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)b1);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), NULL);

    T_CASE("a handle that is not a context gives NULL with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(gdi32, SelectObject, NOT_A_CONTEXT, b1), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("NULL restores the default into the slot that most recently left it, and reverse NULLs end where they began");
    T_EQ(CALL(gdi32, SelectObject, mem, b1), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, p), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)p);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)b1);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), NULL);

    T_CASE("selecting what is selected returns it and changes nothing");
    T_EQ(CALL(gdi32, SelectObject, mem, b2), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, b2), (HGDIOBJ)b2);
    T_EQ(CALL(gdi32, SelectObject, mem, b2), (HGDIOBJ)b2);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)b2);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), NULL);

    T_CASE("a bitmap selected into another context is NULL");
    mem2 = rt_dc_create_compatible(NULL);
    T_TRUE(mem2 != NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, bm), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem2, bm), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)bm);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), NULL);

    T_CASE("a pen or font may be selected into two contexts");
    T_EQ(CALL(gdi32, SelectObject, mem, p), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem2, p), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, f), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem2, f), NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, p), (HGDIOBJ)p);
    T_EQ(CALL(gdi32, SelectObject, mem2, p), (HGDIOBJ)p);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)f);
    T_EQ(CALL(gdi32, SelectObject, mem2, NULL), (HGDIOBJ)f);
    T_EQ(rt_dc_delete(mem2), TRUE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    prev = CALL(gdi32, SelectObject, mem, b1);
    T_EQ(prev, NULL);
    T_EQ(CALL(gdi32, SelectObject, mem, NULL), (HGDIOBJ)b1);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a region is a stop of the core (rt_dc_select, its comment). */
    rgn = rt_gdi_create_rect_rgn(0, 0, 4, 4);
    T_TRAPS(CALL(gdi32, SelectObject, mem, rgn), "rt_dc_select: not implemented");

    T_EQ(rt_dc_delete(mem), TRUE);
}
