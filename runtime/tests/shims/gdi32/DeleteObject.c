#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/gdi.h"

/* The oracle is the core: rt_gdi_delete of a handle that is gone (or was never an object) is
 * FALSE, and of a live brush TRUE once.  A window handle is a handle of another kind: the
 * core does not treat it as an object, so DeleteObject returns FALSE for it (rt/gdi.h). */

/* A window procedure that passes every message on to the default one. */
GUEST_FN(rec)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    tf_fp_enter_host();
    const LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

static HWND mk_window(void)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = u"T" };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = WS_POPUP,.cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"), .x = 0, .y = 0,
                         .cx = 100, .cy = 100, .parent = NULL, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(gdi32, DeleteObject)
{
    HGDIOBJ b;
    HDC m;
    HWND w;

    T_CASE("deletes a brush once and fails the second time");
    b = rt_gdi_create_brush(0x00030201u);
    T_REQUIRE(b != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, DeleteObject, b), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* success leaves last error alone */
    T_EQ(CALL(gdi32, DeleteObject, b), FALSE);    /* the record is gone */
    T_EQ(rt_gdi_delete(b), FALSE);

    T_CASE("NULL and a window handle fail");
    w = mk_window();
    T_REQUIRE(w != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, DeleteObject, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);                 /* not an object: last error untouched */
    T_EQ(CALL(gdi32, DeleteObject, (HGDIOBJ)w), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a brush selected into a memory context is a stop of the core (rt_gdi_delete). */
    b = rt_gdi_create_brush(0x00060504u);
    T_REQUIRE(b != NULL);
    m = rt_dc_create_compatible(NULL);
    T_REQUIRE(m != NULL);
    (void)rt_dc_select(m, b);                     /* the brush is now selected into m */
    T_TRAPS(CALL(gdi32, DeleteObject, b), "rt_gdi_delete: not implemented");
}
