/* user32!RegisterRawInputDevices (shim: runtime/shims/user32/RegisterRawInputDevices.c).  The record
 * is the process's, so the cases run in order on one record.  Expected values: Wine's
 * user32/tests/input.c, test_RegisterRawInputDevices, and the Microsoft page of the function. */
#include "rt/test.h"
#include "rt/rawinput.h"
#include "rt/window.h"
#include "rt/win/user.h"

GUEST_FN(regrawinput_proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }  /* rdx: the message */

TEST(user32, RegisterRawInputDevices)
{
    Ctx *ctx = T_CTX();
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(regrawinput_proc), .lpszClassName = u"ShimRawT" };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"ShimRawT"), .title = RT_TEXT_W(u"Raw"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(ctx, &a);
    RAWINPUTDEVICE plain[2] = { { .usUsagePage = 1, .usUsage = 5, .dwFlags = 0, .hwndTarget = NULL },
                                { .usUsagePage = 1, .usUsage = 4, .dwFlags = 0, .hwndTarget = NULL } };
    RAWINPUTDEVICE sink[2] = { { .usUsagePage = 1, .usUsage = 5, .dwFlags = RIDEV_INPUTSINK, .hwndTarget = NULL },
                               { .usUsagePage = 1, .usUsage = 4, .dwFlags = RIDEV_INPUTSINK, .hwndTarget = NULL } };
    RAWINPUTDEVICE tgt[2] = { { .usUsagePage = 1, .usUsage = 5, .dwFlags = RIDEV_INPUTSINK, .hwndTarget = h },
                              { .usUsagePage = 1, .usUsage = 4, .dwFlags = RIDEV_INPUTSINK, .hwndTarget = h } };
    RAWINPUTDEVICE game = { .usUsagePage = 1, .usUsage = 2, .dwFlags = 0, .hwndTarget = h };
    RAWINPUTDEVICE devnotify = { .usUsagePage = 1, .usUsage = 6, .dwFlags = RIDEV_DEVNOTIFY, .hwndTarget = NULL };
    RAWINPUTDEVICE unreg = { .usUsagePage = 1, .usUsage = 2, .dwFlags = RIDEV_REMOVE, .hwndTarget = h };
    RAWINPUTDEVICE got = { 0 };

    T_REQUIRE(h != NULL);

    T_CASE("(NULL, 0, 0) and a good list of two with size 0 fail with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, RegisterRawInputDevices, NULL, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, RegisterRawInputDevices, plain, 2, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("two entries (page 1, usages 5 and 4, flags 0, no target) with size 16 succeed and leave last error alone, and two collections are registered");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, RegisterRawInputDevices, plain, 2, 16), TRUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_EQ(rt_raw_input_count(), 2);

    T_CASE("the same two with RIDEV_INPUTSINK and no target fail with ERROR_INVALID_PARAMETER and record nothing");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, RegisterRawInputDevices, sink, 2, 16), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_raw_input_count(), 2);
    T_TRUE(rt_raw_input_find(1, 5, &got, NULL));
    T_EQ(got.dwFlags, 0);
    T_TRUE(got.hwndTarget == NULL);

    T_CASE("the same two with RIDEV_INPUTSINK and a window as target succeed, and the record of usage 5 has the flag and the window");
    T_EQ(CALL(user32, RegisterRawInputDevices, tgt, 2, 16), TRUE);
    T_EQ(rt_raw_input_count(), 2);
    T_TRUE(rt_raw_input_find(1, 5, &got, NULL));
    T_EQ(got.dwFlags, RIDEV_INPUTSINK);
    T_TRUE(got.hwndTarget == h);

    T_CASE("one entry (page 1, usage 2, the window as target) with size 16, the game's shape, succeeds");
    T_EQ(CALL(user32, RegisterRawInputDevices, &game, 1, 16), TRUE);
    T_EQ(rt_raw_input_count(), 3);

    /* The two stops this test reaches; each is the core's fatal report, so the shim adds no guard. */
    T_TRAPS(CALL(user32, RegisterRawInputDevices, &devnotify, 1, 16), "rt_raw_input_register: not implemented: flags 0x2000");
    T_TRAPS(CALL(user32, RegisterRawInputDevices, &unreg, 1, 16), "rt_raw_input_register: not implemented: RIDEV_REMOVE with a target window");
}
