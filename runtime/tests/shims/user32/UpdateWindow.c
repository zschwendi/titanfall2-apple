#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: the comment of rt_window_update in rt/window.h (UpdateWindow's page;
 * Wine user32/tests/msg.c, test_paint_messages; review V35). */
static UINT seen[32];
static unsigned nseen;

/* A window procedure that records the message numbers and passes everything on. */
GUEST_FN(rec)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    if (nseen < 32) seen[nseen++] = msg;
    tf_fp_enter_host();
    const LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

__attribute__((unused)) static HWND mk(DWORD style, HWND parent, INT x, INT y, INT cx, INT cy)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = u"T", .cbWndExtra = 16 };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = x, .y = y, .cx = cx, .cy = cy, .parent = parent, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, UpdateWindow)
{
    HWND h = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(h != NULL);

    T_CASE("TRUE for a record, nothing is painted");
    nseen = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UpdateWindow, h), TRUE);
    T_EQ(nseen, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL and a handle that is no window");
    nseen = 0;
    T_EQ(CALL(user32, UpdateWindow, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UpdateWindow, (HWND)0x40000FF0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    HWND gone = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(gone != NULL);
    rt_window_destroy(T_CTX(), gone);
    nseen = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UpdateWindow, gone), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(nseen, 0);
}
