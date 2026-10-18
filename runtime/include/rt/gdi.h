/* Device contexts and GDI objects with no screen (gdi32.dll, and GetDC/ReleaseDC of user32).
 * The contract is docs/handoff/rt-contract-window.md; rt/window.h has the rules this header
 * shares, word for word: NO SCREEN, SOURCES (a statement that names a Wine test, a Microsoft
 * page or a V, C, D or Z row of docs/reviews/contracts-check.md, contracts-check-2.md or
 * win-behaviour-check-5.md is checked; an N or S row is one that could NOT be settled; every
 * other statement about Windows here is from memory, not verified), STOPS, LAST ERROR
 * (convention 1), LOCKS (the same one lock)
 * and HANDLES (device contexts and objects are in the "user" table).
 *
 *     piece        file                 state        what
 *     gdi-dc       core/gdi_dc.c        built        device-context records and their attributes
 *     gdi-object   core/gdi_object.c    [not built]  pens, brushes, fonts, bitmaps, regions as records
 *
 * NOTHING IS DRAWN.  A device context is a record that remembers what was selected into it
 * and which attributes were set; an object is a record of how it was made.  No function here
 * reads or writes a pixel, and the functions that would (BitBlt, StretchBlt, ExtTextOut,
 * DrawText, Polygon, Polyline, Rectangle, SetPixel, GetPixel, SetDIBits, GetDIBits, DrawIconEx)
 * have no core function: their imports stay stops until Gate 4.  The one piece of memory a
 * guest draws into itself, the bits of a DIB section, is real guest memory.
 *
 * LAST ERROR.  Given a handle that is not a device context, Windows sets ERROR_INVALID_HANDLE
 * (6): Wine gdi32/tests/dc.c, test_dc_values, asserts it for SetBkMode and GetDeviceCaps
 * (review C9).  The other attribute functions were not tested there; the core does the same
 * for all of them (rt_dc_attr, rt_dc_set_point, rt_dc_device_cap), which is the likely
 * reading and is marked "from memory" where it goes beyond the two tested functions.  Apart
 * from that a function here sets last error only where its comment names a code, and leaves
 * it alone otherwise (from memory: gdi32 mostly does not set it).
 */
#ifndef RT_GDI_H
#define RT_GDI_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/base.h"
#include "rt/win/gdi.h"

/* ---- device contexts (core/gdi_dc.c) [built: gdi-dc] ------------------------------------------- */
/* Three kinds of record: a display context (GetDC of NULL, of RT_HWND_DESKTOP or of a window;
 * CreateDC of the display), and a memory context (CreateCompatibleDC).  A new context has:
 * text colour 0x000000, background colour 0xFFFFFF, background mode OPAQUE, text alignment
 * TA_LEFT | TA_TOP (0), stretch mode BLACKONWHITE, viewport origin and current position
 * {0, 0}, no pen, brush or font selected by the guest (the stock defaults stand in as the
 * handle value NULL: GetStockObject is not imported, so no stock handle ever reaches the
 * core), and for a memory context a 1 x 1 monochrome bitmap, likewise NULL.  The two colours
 * are what Wine gdi32/tests/dc.c (test_dc_values) asserts (review V17); the other defaults
 * are Microsoft's "Display Device Context Defaults", SetTextAlign's page, CreateCompatibleDC's
 * page and "Memory Device Contexts" (review V46).  WHERE THIS DIFFERS FROM WINDOWS, by this
 * contract's choice: the first SelectObject into a new context returns the stock object's
 * handle on Windows, and SelectObject(dc, NULL) returns NULL there and changes nothing (Wine
 * gdi32/tests/gdiobj.c, test_gdi_objects), where NULL here means "the stock object": a guest
 * that selects back what it was given gets the right result, one that selects NULL on
 * purpose gets a restore.  CreateCompatibleBitmap(dc, 0, 0) and CreateBitmap(0, ...) return
 * the one stock bitmap, and CreatePen(PS_NULL, ...) the one stock null pen, the same handle
 * every time (Wine gdi32/tests/bitmap.c, test_CreateBitmap; pen.c, test_logpen); a guest that
 * compares such a handle is told NULL, or a new record, here. */

/* GetDC(hWnd).  NULL with ERROR_INVALID_WINDOW_HANDLE for a handle that is not a window (NULL
 * is documented; the code is from memory, review S25).
 * ReleaseDC(hWnd, hDC): 1 and the record is gone, for a context that rt_dc_get gave out and
 * that has not been released, WHATEVER window `h` names: the window is not compared.  0, and
 * nothing changes, for NULL, for a context that rt_dc_get did not give out and for one
 * already released.  (Wine user32/tests/dce.c, test_parameters: "ReleaseDC with wrong window
 * should succeed", "second ReleaseDC should fail"; review D12.)  Sets no last error. */
HDC rt_dc_get(HWND h);
INT rt_dc_release(HWND h, HDC dc);

/* Which window a context from rt_dc_get looks at (core/gdi_dc_window.c; unit gate3-run-3, for
 * EnumDisplayMonitors with a context, which needs the context's visible region and has no
 * other way to it).  1: `dc` is a live context that rt_dc_get gave out; *window is the window
 * it was given, NULL for the screen (GetDC(NULL) and GetDC of the desktop), and *viewport is
 * its viewport origin.  -1: a live context of another kind (CreateCompatibleDC, CreateDC).
 * 0: not a context, or one already released.  Sets no last error; writes nothing unless it
 * returns 1.  The record is what rt_dc_get stored: a window destroyed since is still named. */
int rt_dc_window(HDC dc, HWND *window, POINT *viewport);

/* CreateCompatibleDC(hdc): a memory context.  `dc` NULL means the screen (Wine
 * gdi32/tests/dc.c, test_CreateCompatibleDC).  NULL for a `dc` that is not a context (no
 * last error set: from memory, review S26).
 * CreateDCW(pwszDriver, pwszDevice, NULL, NULL), names compared without regard to ASCII case
 * (Wine gdi32/tests/dc.c, test_CreateDC; review D13).  A display context for:
 *   driver L"DISPLAY"          whatever the device is (NULL, the adapter, any text)
 *   driver L"\\.\DISPLAY1"     (the adapter's name) whatever the device is
 *   driver NULL                with device L"\\.\DISPLAY1"
 * NULL for (NULL, NULL), for driver NULL with device L"DISPLAY", and for a monitor's name
 * (L"\\.\DISPLAY1\Monitor0") in place of the adapter's (the same test); NULL also for any
 * other driver name: no other device exists (from memory).  No last error is set (from
 * memory: the test's codes were not read).
 * DeleteDC: TRUE and the record is gone, whatever is selected into it stays alive; FALSE for
 * a handle that is not a context (no last error set: from memory, S26).  STOPS: DeleteDC of
 * a context that rt_dc_get gave out (Windows returns TRUE and the context is gone, dc.c,
 * test_DeleteDC, though DeleteDC's page says it must be released instead; not built). */
HDC  rt_dc_create_compatible(HDC dc);
HDC  rt_dc_create_display(const WCHAR *driver, const WCHAR *device);
BOOL rt_dc_delete(HDC dc);

/* GetDeviceCaps(hdc, index): rt_desktop_device_cap (rt/window.h) for every context, memory
 * contexts included (a memory context is compatible with the screen).  0 with
 * ERROR_INVALID_HANDLE for a handle that is not a context (Wine gdi32/tests/dc.c,
 * test_dc_values; review C9).  STOPS: an index rt_desktop_device_cap does not answer. */
INT rt_dc_device_cap(HDC dc, INT index);

/* SelectObject(hdc, h): select a pen, brush, font, or (memory context only) bitmap, and
 * return the handle selected before, which is NULL the first time (see the defaults above;
 * a guest that selects that NULL back restores the default).
 * NULL with ERROR_INVALID_HANDLE (6) for a `dc` that is not a context, looked at first (Wine
 * gdi32/tests/gdiobj.c, test_gdi_objects; bitmap.c, test_select_object; review D14).
 * NULL with no last error set (from memory) for an `obj` that is not an object (other than
 * the NULL just described), and for a bitmap into a display context (Wine
 * gdi32/tests/bitmap.c, test_CreateBitmap; review V17) or one that is selected into another
 * context (SelectObject's page; review V49).
 * `obj` NULL (this contract's choice, see the top of this section): the stock default goes
 * back into the slot (pen, brush, font or bitmap) that most recently left its default, and
 * the handle that was there is returned; NULL and no change when every slot is at its
 * default.  So a guest that selects back, in reverse order, the NULLs it was given ends
 * where it began.  Selecting what is selected already returns that handle and changes nothing.
 * A pen, brush or font may be selected into several contexts at once.
 * STOPS: a region (it would set the clip region and return a region type, not a handle:
 * SelectObject's page, V49). */
HGDIOBJ rt_dc_select(HDC dc, HGDIOBJ obj);

/* The attributes one value wide: SetBkColor/GetBkColor, SetTextColor/GetTextColor,
 * SetBkMode, SetTextAlign, SetStretchBltMode.  If `set` is not NULL the attribute becomes
 * *set; *old (if not NULL) receives the value before.  false with ERROR_INVALID_HANDLE for a
 * handle that is not a context (asserted for SetBkMode by Wine gdi32/tests/dc.c,
 * test_dc_values, review C9; for the other four attributes and for the two getters it is
 * from memory: the same test asserts only GetBkColor's CLR_INVALID, not its last error).
 * false also for a value Windows refuses: a background mode other than TRANSPARENT and
 * OPAQUE (no last error set), a stretch mode outside 1 to 4 (ERROR_INVALID_PARAMETER, the
 * code SetStretchBltMode's page names); which values Windows refuses is from memory (review
 * S27, not settled); nothing is stored.  A colour is stored as given.  The shim turns false
 * into its function's failure value: CLR_INVALID for the colours, 0 for the modes,
 * 0xFFFFFFFF (GDI_ERROR) for the alignment (the three functions' pages; dc.c,
 * test_dc_values; review V48). */
typedef enum { RT_DC_BK_COLOR = 1, RT_DC_TEXT_COLOR, RT_DC_BK_MODE, RT_DC_TEXT_ALIGN, RT_DC_STRETCH_MODE } RtDcAttr;
bool rt_dc_attr(HDC dc, RtDcAttr which, const ULONG *set, ULONG *old);

/* SetViewportOrgEx and MoveToEx: store the point; *old (if not NULL) receives the one
 * before.  FALSE with ERROR_INVALID_HANDLE for a handle that is not a context (the code: from
 * memory, by the rule of LAST ERROR above; these two were not among the functions tested). */
typedef enum { RT_DC_VIEWPORT_ORG = 1, RT_DC_POSITION } RtDcPoint;
BOOL rt_dc_set_point(HDC dc, RtDcPoint which, INT x, INT y, POINT *old);

/* ---- objects (core/gdi_object.c) [not built: gdi-object] --------------------------------------- */
/* Each returns the handle of a new record, or NULL with ERROR_NOT_ENOUGH_MEMORY when the
 * table is full.  A record keeps exactly what GetObject must give back. */

/* CreatePen(style, width, colour).  The record keeps the style, the ABSOLUTE VALUE of the
 * width (-123 reads back 123) and the colour.  Style 5 (PS_NULL) is the null pen: its record
 * reads back {5, {1, 0}, 0} whatever width and colour were passed (on Windows it is the one
 * stock null pen; see the top of this section).  Wine gdi32/tests/pen.c, test_logpen; review
 * D15.  STOPS: style outside 0 to 6. */
HPEN   rt_gdi_create_pen(INT style, INT width, COLORREF color);
HBRUSH rt_gdi_create_brush(COLORREF color);                         /* CreateSolidBrush */

/* CreateFontA: the shim fills a LOGFONTA from its 14 arguments (a NULL face name is the
 * empty string; a name of 32 bytes or more is cut to 31).  No font is matched or loaded: a
 * font record is its LOGFONT, which is also all Windows does until the font is used to draw. */
HFONT rt_gdi_create_font_a(const LOGFONTA *lf);

/* CreateBitmap(w, h, planes, bpp, bits) and CreateCompatibleBitmap(hdc, w, h): a bitmap
 * record with its size and format and NO pixel storage.  A compatible bitmap has the
 * screen's format (1 plane, 32 bits) for a display context, and the format of the bitmap
 * selected into it for a memory context (so 1 x 1 monochrome gives a monochrome bitmap, the
 * documented trap).  Width or height 0 gives a 1 x 1 monochrome bitmap, and that is looked
 * at BEFORE the stops below: with a zero size Windows accepts planes 0 and bpp 0.  (The
 * pages of CreateCompatibleBitmap and CreateBitmap; Wine gdi32/tests/bitmap.c,
 * test_CreateBitmap and test_createdibitmap; review V47.)  NULL for a `dc` that is not a
 * context (from memory).
 * STOPS: `bits` not NULL (initial pixels); a negative size; planes other than 1; bpp other
 * than 1, 8, 16, 24, 32. */
HBITMAP rt_gdi_create_bitmap(INT w, INT h, UINT planes, UINT bpp, const void *bits);
HBITMAP rt_gdi_create_compatible_bitmap(HDC dc, INT w, INT h);

/* CreateDIBSection(hdc, pbmi, usage, ppvBits, hSection, offset): a bitmap record whose
 * pixels are zero-filled guest memory that the guest reads and writes itself: *bits
 * receives the address.  The memory is rt_vm_alloc'ed (rt/vm.h; committed, read-write) and
 * freed when the record is deleted.  Size: ((biWidth * biBitCount + 31) / 32) * 4 bytes a
 * row, |biHeight| rows.  NULL with ERROR_INVALID_PARAMETER for pbmi NULL, a biSize below
 * 40 other than 12, a width <= 0 or a height of 0; *bits is then set to NULL.  (CreateDIBSection's
 * page; Wine gdi32/tests/bitmap.c, test_dibsections, test_dib_formats, test_dib_info and, for
 * the sizes 39 and 13, test_bitmapinfoheadersize; review V50.)
 * STOPS: a biSize of 12 (a BITMAPCOREHEADER, which Windows accepts: test_bitmapinfoheadersize;
 * review D18; not built, no caller seen); hSection not NULL; usage other than DIB_RGB_COLORS; biPlanes other than 1; a
 * biBitCount other than 8, 16, 24, 32; a biCompression other than BI_RGB and BI_BITFIELDS;
 * more than 256 MB. */
HBITMAP rt_gdi_create_dib_section(HDC dc, const BITMAPINFO *bmi, UINT usage, void **bits, HANDLE section, DWORD offset);

/* CreateRectRgn and SetRectRgn: a region record that is one rectangle (corners are put in
 * order; an empty rectangle is the empty region).  rt_gdi_set_rect_rgn: FALSE for a handle
 * that is not a region. */
HRGN rt_gdi_create_rect_rgn(INT left, INT top, INT right, INT bottom);
BOOL rt_gdi_set_rect_rgn(HRGN rgn, INT left, INT top, INT right, INT bottom);

/* DeleteObject: TRUE and the record is gone (a DIB section's memory with it); FALSE for a
 * handle that is not an object, NULL included, last error untouched (Wine
 * gdi32/tests/gdiobj.c, test_gdi_objects; review V52).  STOPS: an object that is selected
 * into a context (Windows returns FALSE: DeleteObject's page, return value, review V53; which
 * last error it leaves is not shown there, so no answer is invented). */
BOOL rt_gdi_delete(HGDIOBJ obj);

/* GetObjectA/W(h, cb, out): what the record was made from, copied to `out`, and the number
 * of bytes copied.  `out` NULL: the size of the type's first structure below, whatever `cb`
 * is: 16, 16, 60 or 92, and 32 for every bitmap, a DIB section included (NOT 104: Wine
 * gdi32/tests/bitmap.c, test_dib_info, asserts GetObjectW(dib, 0, NULL) == sizeof(BITMAP);
 * docs/reviews/win-behaviour-check-5.md Z3; contracts-check-2.md D17).  By type (Wine
 * gdi32/tests: pen.c test_logpen, brush.c test_solidbrush, font.c check_font and
 * test_logfont, bitmap.c test_bitmap_info and test_dib_info; review V51):
 *   pen     LOGPEN (16): style, {width, 0}, colour as rt_gdi_create_pen keeps them
 *   brush   LOGBRUSH (16): BS_SOLID, colour, 0
 *   font    LOGFONTA (60) or, `unicode`, LOGFONTW (92), the face name converted from code page 1252
 *   bitmap  BITMAP (32): type 0, width, height (a DIB section: the ABSOLUTE VALUE of the
 *           height it was made with), row bytes (rounded up to 2 bytes; a DIB section: to
 *           4), 1 plane, bits per pixel, bmBits NULL (a DIB section: its memory);
 *           a DIB section with cb >= 104: DIBSECTION (104): that BITMAP, then dsBmih with
 *           biWidth, biPlanes, biBitCount and the two resolutions as given and biHeight the
 *           ABSOLUTE VALUE of the height the section was made with (a top-down section made
 *           with -100 reports 100: test_dib_info; Z3, D16); biSizeImage the image size in
 *           bytes, row bytes times rows (from memory: the test clears the field before it
 *           compares, so Windows' value is not settled); biCompression and the three masks:
 *             made with BI_BITFIELDS     BI_BITFIELDS and the masks given
 *             16 bits, made with BI_RGB  BI_BITFIELDS (3) and the 5-5-5 masks 0x7C00, 0x03E0,
 *                                        0x001F (Wine gdi32/tests/dib.c, test_simple_graphics; D16)
 *             8, 24, 32 bits, BI_RGB     BI_RGB and masks 0 (24 and 32: the same test; 8: from memory)
 *           then dshSection NULL, dsOffset 0.
 *           The cases its test must have (Z3, D16): a section of width 16, height -16, 32
 *           bits; rt_gdi_get_object(h, 104, &ds, true) returns 104 with ds.dsBmih.biHeight ==
 *           16 and ds.dsBm.bmHeight == 16, and rt_gdi_get_object(h, 0, NULL, true) returns 32;
 *           a section of 4 x 4 at 16 bits BI_RGB reports biCompression 3 and the masks
 *           {0x7C00, 0x3E0, 0x1F}.
 * 0 for a handle that is not an object (NULL: gdiobj.c, test_gdi_objects; V52), for a region
 * (GetObject's page does not list regions; V51), and for a cb below the structure's size
 * (nothing is written; Windows returns 0 for pens and bitmaps, Wine gdi32/tests pen.c
 * test_logpen and bitmap.c test_dib_info; for a font with cb below 60: from memory, review
 * S28). */
INT rt_gdi_get_object(HANDLE obj, INT cb, void *out, bool unicode);

#endif /* RT_GDI_H */
