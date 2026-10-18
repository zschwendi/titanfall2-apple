/* GDI object records (specification: rt/gdi.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `gdi-object` (docs/handoff/rt-contract-window.md).  Tests: tests/core/gdi_object.c.
 * Every function below is declared and specified in rt/gdi.h.
 *
 * STATE: none of its own; the RtGdiObj records (core/win.c makes them), read and written under the
 * win lock.  Nothing here calls guest code or draws.
 */
#include <string.h>

#include "win_priv.h"
#include "rt/vm.h"
#include "rt/locale.h"

#define PEN_STYLE_NULL 5                    /* PS_NULL: the null pen (rt/gdi.h) */

/* Win lock held: the record's bits, if it is a DIB section, go back to the system. */
static void *dib_bits_of(RtGdiObj *o)
{
    return o->type == RT_GDI_BITMAP && o->u.bitmap.dib ? o->u.bitmap.bits : NULL;
}

HPEN rt_gdi_create_pen(INT style, INT width, COLORREF color)
{
    if (style < 0 || style > 6) RT_WIN_STOP("rt_gdi_create_pen", "style %d", style);
    HGDIOBJ h = rt_win_gdiobj_new(RT_GDI_PEN);
    if (!h) return NULL;
    RtGdiObj *o = rt_win_gdiobj_ref(h);
    rt_win_lock();
    if (style == PEN_STYLE_NULL) {
        o->u.pen.style = PEN_STYLE_NULL; o->u.pen.width = 1; o->u.pen.color = 0;
    } else {
        o->u.pen.style = style;
        o->u.pen.width = width < 0 ? (INT)(0u - (unsigned)width) : width;   /* absolute value, no overflow */
        o->u.pen.color = color;
    }
    rt_win_unlock();
    rt_object_unref(&o->obj);
    return (HPEN)h;
}

HBRUSH rt_gdi_create_brush(COLORREF color)
{
    HGDIOBJ h = rt_win_gdiobj_new(RT_GDI_BRUSH);
    if (!h) return NULL;
    RtGdiObj *o = rt_win_gdiobj_ref(h);
    rt_win_lock();
    o->u.brush.color = color;
    rt_win_unlock();
    rt_object_unref(&o->obj);
    return (HBRUSH)h;
}

HFONT rt_gdi_create_font_a(const LOGFONTA *lf)
{
    if (!lf) { rt_thread_set_last_error(ERROR_INVALID_PARAMETER); return NULL; }   /* from memory: not in the spec */
    HGDIOBJ h = rt_win_gdiobj_new(RT_GDI_FONT);
    if (!h) return NULL;
    RtGdiObj *o = rt_win_gdiobj_ref(h);
    rt_win_lock();
    o->u.font = *lf;
    o->u.font.lfFaceName[31] = 0;                    /* a name of 32 bytes or more is cut to 31 */
    rt_win_unlock();
    rt_object_unref(&o->obj);
    return (HFONT)h;
}

/* A new bitmap record of the given size and format, with no pixel storage (win lock taken here). */
static HBITMAP bitmap_new(INT w, INT h, UINT bpp)
{
    HGDIOBJ x = rt_win_gdiobj_new(RT_GDI_BITMAP);
    if (!x) return NULL;
    RtGdiObj *o = rt_win_gdiobj_ref(x);
    rt_win_lock();
    o->u.bitmap.width = w;
    o->u.bitmap.height = h;
    o->u.bitmap.bpp = bpp;
    rt_win_unlock();
    rt_object_unref(&o->obj);
    return (HBITMAP)x;
}

HBITMAP rt_gdi_create_bitmap(INT w, INT h, UINT planes, UINT bpp, const void *bits)
{
    if (bits) RT_WIN_STOP("rt_gdi_create_bitmap", "initial pixels (%p)", bits);
    if (w < 0 || h < 0) RT_WIN_STOP("rt_gdi_create_bitmap", "size %d x %d", w, h);
    if (w == 0 || h == 0) { w = 1; h = 1; bpp = 1; }    /* before the stops below: planes and bpp are not looked at */
    else {
        if (planes != 1) RT_WIN_STOP("rt_gdi_create_bitmap", "%u planes", planes);
        if (bpp != 1 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
            RT_WIN_STOP("rt_gdi_create_bitmap", "%u bits a pixel", bpp);
    }
    return bitmap_new(w, h, bpp);
}

HBITMAP rt_gdi_create_compatible_bitmap(HDC dc, INT w, INT h)
{
    RtDc *d = rt_win_dc_ref(dc);
    if (!d) return NULL;                             /* from memory: no last error (review; spec silent) */
    if (w < 0 || h < 0) { rt_object_unref(&d->obj); RT_WIN_STOP("rt_gdi_create_compatible_bitmap", "size %d x %d", w, h); }
    UINT bpp = 32;                                   /* a display context: the screen's format */
    if (d->kind == RT_DC_MEMORY) {
        bpp = 1;                                     /* nothing selected: the 1 x 1 monochrome default */
        rt_win_lock();
        RtGdiObj *sel = d->sel[RT_GDI_BITMAP] ? (RtGdiObj *)rt_htab_ref(rt_win_table(), d->sel[RT_GDI_BITMAP], RT_K_GDIOBJ) : NULL;
        if (sel) bpp = sel->u.bitmap.bpp;
        rt_win_unlock();
        if (sel) rt_object_unref(&sel->obj);
    }
    rt_object_unref(&d->obj);
    if (w == 0 || h == 0) { w = 1; h = 1; bpp = 1; }
    return bitmap_new(w, h, bpp);
}

#define DIB_MAX_BYTES ((uint64_t)256 << 20)     /* more than this is a stop (rt/gdi.h) */

/* A DIB section that could not be made: NULL with ERROR_INVALID_PARAMETER, *bits NULL. */
static HBITMAP dib_refuse(void **bits)
{
    if (bits) *bits = NULL;
    rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
    return NULL;
}

HBITMAP rt_gdi_create_dib_section(HDC dc, const BITMAPINFO *bmi, UINT usage, void **bits, HANDLE section, DWORD offset)
{
    (void)dc; (void)offset;                      /* dsOffset is always 0 (rt/gdi.h) */
    if (!bmi) return dib_refuse(bits);
    const BITMAPINFOHEADER *hd = &bmi->bmiHeader;
    if (hd->biSize == 12) RT_WIN_STOP("rt_gdi_create_dib_section", "a BITMAPCOREHEADER (biSize 12)");
    if (hd->biSize < 40) return dib_refuse(bits);
    if (hd->biWidth <= 0 || hd->biHeight == 0) return dib_refuse(bits);
    if (section) RT_WIN_STOP("rt_gdi_create_dib_section", "a file mapping (%p)", (void *)section);
    if (usage != DIB_RGB_COLORS) RT_WIN_STOP("rt_gdi_create_dib_section", "usage %u", usage);
    if (hd->biPlanes != 1) RT_WIN_STOP("rt_gdi_create_dib_section", "%u planes", (unsigned)hd->biPlanes);
    const UINT bpp = hd->biBitCount;
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) RT_WIN_STOP("rt_gdi_create_dib_section", "%u bits a pixel", bpp);
    const DWORD comp = hd->biCompression;
    if (comp != BI_RGB && comp != BI_BITFIELDS) RT_WIN_STOP("rt_gdi_create_dib_section", "compression %lu", (unsigned long)comp);

    const uint64_t row = (((uint64_t)hd->biWidth * bpp + 31) / 32) * 4;
    const int64_t hh = hd->biHeight;
    const uint64_t rows = (uint64_t)(hh < 0 ? -hh : hh);
    if (rows > DIB_MAX_BYTES / row) RT_WIN_STOP("rt_gdi_create_dib_section", "more than 256 MB (%u x %u)", (unsigned)hd->biWidth, (unsigned)rows);
    const uint64_t bytes = row * rows;

    DWORD masks[3] = { 0, 0, 0 };
    DWORD out_comp = comp;
    if (comp == BI_BITFIELDS) memcpy(masks, (const uint8_t *)bmi + 40, sizeof masks);   /* the masks follow the header */
    else if (bpp == 16) { out_comp = BI_BITFIELDS; masks[0] = 0x7C00; masks[1] = 0x03E0; masks[2] = 0x001F; }

    uint64_t addr = rt_vm_alloc(0, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    if (!addr) return dib_refuse(bits);              /* from memory: no memory left */
    HGDIOBJ x = rt_win_gdiobj_new(RT_GDI_BITMAP);
    if (!x) { (void)rt_vm_free(addr, 0, MEM_RELEASE); return dib_refuse(bits); }
    RtGdiObj *o = rt_win_gdiobj_ref(x);
    rt_win_lock();
    o->u.bitmap.width = hd->biWidth;
    o->u.bitmap.height = hd->biHeight;
    o->u.bitmap.bpp = bpp;
    o->u.bitmap.dib = true;
    o->u.bitmap.bits = (void *)(uintptr_t)addr;
    o->u.bitmap.bytes = (size_t)bytes;
    o->u.bitmap.compression = out_comp;
    memcpy(o->u.bitmap.masks, masks, sizeof masks);
    o->u.bitmap.x_ppm = hd->biXPelsPerMeter;
    o->u.bitmap.y_ppm = hd->biYPelsPerMeter;
    rt_win_unlock();
    rt_object_unref(&o->obj);
    if (bits) *bits = (void *)(uintptr_t)addr;
    return (HBITMAP)x;
}

HRGN rt_gdi_create_rect_rgn(INT left, INT top, INT right, INT bottom)
{
    HGDIOBJ x = rt_win_gdiobj_new(RT_GDI_REGION);
    if (!x) return NULL;
    RtGdiObj *o = rt_win_gdiobj_ref(x);
    rt_win_lock();
    o->u.region.left = left < right ? left : right;  o->u.region.right = left < right ? right : left;
    o->u.region.top = top < bottom ? top : bottom;   o->u.region.bottom = top < bottom ? bottom : top;
    rt_win_unlock();
    rt_object_unref(&o->obj);
    return (HRGN)x;
}

BOOL rt_gdi_set_rect_rgn(HRGN rgn, INT left, INT top, INT right, INT bottom)
{
    RtGdiObj *o = rt_win_gdiobj_ref((HGDIOBJ)rgn);
    if (!o) return FALSE;                            /* sets no last error */
    BOOL ok = FALSE;
    rt_win_lock();
    if (o->self && o->type == RT_GDI_REGION) {
        o->u.region.left = left < right ? left : right;  o->u.region.right = left < right ? right : left;
        o->u.region.top = top < bottom ? top : bottom;   o->u.region.bottom = top < bottom ? bottom : top;
        ok = TRUE;
    }
    rt_win_unlock();
    rt_object_unref(&o->obj);
    return ok;
}

BOOL rt_gdi_delete(HGDIOBJ obj)
{
    RtGdiObj *o = rt_win_gdiobj_ref(obj);
    if (!o) return FALSE;                            /* not an object: last error untouched */
    rt_win_lock();
    const bool live = o->self != NULL;
    const unsigned selected = o->selected;
    void *bits = NULL;
    if (live && !selected) {
        bits = dib_bits_of(o);
        o->self = NULL;
        if (bits) o->u.bitmap.bits = NULL;
    }
    rt_win_unlock();
    if (!live) { rt_object_unref(&o->obj); return FALSE; }
    if (selected) { rt_object_unref(&o->obj); RT_WIN_STOP("rt_gdi_delete", "an object selected into %u context(s) (%p)", selected, (void *)obj); }
    if (bits) (void)rt_vm_free((uint64_t)(uintptr_t)bits, 0, MEM_RELEASE);   /* the DIB's pixels go with it */
    const DWORD e = rt_thread_get_last_error();
    rt_htab_close(rt_win_table(), (HANDLE)obj, RT_K_GDIOBJ);
    rt_thread_set_last_error(e);
    rt_object_unref(&o->obj);
    return TRUE;
}

/* GetObjectA/W.  The record is copied under the win lock; the code page conversion of a face
 * name is done after the lock is released. */
INT rt_gdi_get_object(HANDLE obj, INT cb, void *out, bool unicode)
{
    RtGdiObj *o = rt_win_gdiobj_ref((HGDIOBJ)obj);
    if (!o) return 0;                                /* not an object: sets no last error */
    uint8_t snap[104];
    LOGFONTA font;
    memset(snap, 0, sizeof snap);
    memset(&font, 0, sizeof font);
    rt_win_lock();
    const bool live = o->self != NULL;
    const RtGdiType type = o->type;
    bool is_dib = false;
    if (live && type == RT_GDI_PEN) {
        LOGPEN lp = { 0 };
        lp.lopnStyle = (UINT)o->u.pen.style;
        lp.lopnWidth.x = o->u.pen.width; lp.lopnWidth.y = 0;
        lp.lopnColor = o->u.pen.color;
        memcpy(snap, &lp, sizeof lp);
    } else if (live && type == RT_GDI_BRUSH) {
        LOGBRUSH lb = { 0 };
        lb.lbStyle = BS_SOLID; lb.lbColor = o->u.brush.color; lb.lbHatch = 0;
        memcpy(snap, &lb, sizeof lb);
    } else if (live && type == RT_GDI_FONT) {
        font = o->u.font;
    } else if (live && type == RT_GDI_BITMAP) {
        const bool dib = o->u.bitmap.dib;
        is_dib = dib;
        const LONG h = dib ? (o->u.bitmap.height < 0 ? -o->u.bitmap.height : o->u.bitmap.height) : o->u.bitmap.height;
        const uint64_t rowb = dib ? (((uint64_t)o->u.bitmap.width * o->u.bitmap.bpp + 31) / 32) * 4
                                  : (((uint64_t)o->u.bitmap.width * o->u.bitmap.bpp + 15) / 16) * 2;
        BITMAP bm = { 0 };
        bm.bmWidth = o->u.bitmap.width; bm.bmHeight = h; bm.bmWidthBytes = (LONG)rowb;
        bm.bmPlanes = 1; bm.bmBitsPixel = (WORD)o->u.bitmap.bpp;
        bm.bmBits = dib ? o->u.bitmap.bits : NULL;
        if (!dib) {
            memcpy(snap, &bm, sizeof bm);
        } else {
            DIBSECTION ds = { 0 };
            ds.dsBm = bm;
            ds.dsBmih.biSize = 40;
            ds.dsBmih.biWidth = o->u.bitmap.width; ds.dsBmih.biHeight = h;
            ds.dsBmih.biPlanes = 1; ds.dsBmih.biBitCount = (WORD)o->u.bitmap.bpp;
            ds.dsBmih.biCompression = o->u.bitmap.compression;
            ds.dsBmih.biSizeImage = (DWORD)(rowb * (uint64_t)h);
            ds.dsBmih.biXPelsPerMeter = o->u.bitmap.x_ppm; ds.dsBmih.biYPelsPerMeter = o->u.bitmap.y_ppm;
            for (int i = 0; i < 3; i++) ds.dsBitfields[i] = o->u.bitmap.masks[i];
            memcpy(snap, &ds, sizeof ds);
        }
    }
    rt_win_unlock();
    rt_object_unref(&o->obj);
    if (!live) return 0;
    if (type == RT_GDI_REGION) return 0;             /* GetObject does not list regions (review V51) */

    if (type == RT_GDI_FONT) {
        const INT size = unicode ? 92 : 60;
        if (!out) return size;
        if (cb < size) return 0;
        if (!unicode) { memcpy(out, &font, 60); return 60; }
        LOGFONTW fw;
        memset(&fw, 0, sizeof fw);
        memcpy(&fw, &font, 28);                      /* the five LONGs and eight BYTEs are the same in both */
        (void)rt_mb_to_wide(1252, 0, font.lfFaceName, 32, fw.lfFaceName, 32);
        fw.lfFaceName[31] = 0;
        memcpy(out, &fw, sizeof fw);
        return 92;
    }
    if (type == RT_GDI_BITMAP) {
        if (!out) return 32;                         /* every bitmap, a DIB section included (rt/gdi.h) */
        if (cb < 32) return 0;
        if (is_dib && cb >= 104) { memcpy(out, snap, 104); return 104; }
        memcpy(out, snap, 32);
        return 32;
    }
    /* pen and brush: 16 bytes */
    if (!out) return 16;
    if (cb < 16) return 0;
    memcpy(out, snap, 16);
    return 16;
}
