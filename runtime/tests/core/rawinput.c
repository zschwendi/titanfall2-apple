/* rt/rawinput.h (core/rawinput.c): the process's raw-input registrations.
 * Expected values: Wine's dlls/user32/tests/input.c, test_RegisterRawInputDevices, and
 * Microsoft's pages of RegisterRawInputDevices and RAWINPUTDEVICE, as the header cites them. */
#include <string.h>

#include "rt/test.h"
#include "rt/rawinput.h"
#include "rt/window.h"
#include "rt/win/user.h"

GUEST_FN(rawinput_proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }  /* rdx: the message */

TEST(core, rawinput)
{
    Ctx *ctx = T_CTX();
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rawinput_proc), .lpszClassName = u"RawT" };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"RawT"), .title = RT_TEXT_W(u"Raw"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(ctx, &a);
    T_REQUIRE(h != NULL);
    RAWINPUTDEVICE two[2] = { { 1, 5, 0, NULL }, { 1, 4, 0, NULL } }, got;       /* gamepad, joystick */
    RAWINPUTDEVICE mouse = { 1, 2, 0, h };
    DWORD thread = 77;

    T_CASE("a size other than 16 fails with ERROR_INVALID_PARAMETER and records nothing");
    rt_set_last_error(ctx, 0xdeadbeef);
    T_EQ(rt_raw_input_register(NULL, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(ctx, 0xdeadbeef);
    T_EQ(rt_raw_input_register(two, 2, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_raw_input_count(), 0);

    T_CASE("gamepad and joystick usages register on a machine that has neither; last error is left alone");
    rt_set_last_error(ctx, 0xdeadbeef);
    T_EQ(rt_raw_input_register(two, 2, sizeof(RAWINPUTDEVICE)), TRUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_EQ(rt_raw_input_count(), 2);
    T_EQ(rt_raw_input_find(1, 4, &got, &thread), TRUE);
    T_EQ(got.usUsagePage, 1);
    T_EQ(got.usUsage, 4);
    T_EQ(got.dwFlags, 0);
    T_TRUE(got.hwndTarget == NULL);
    T_EQ(thread, 0);
    T_EQ(rt_raw_input_find(1, 2, NULL, NULL), FALSE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "RegisterRawInputDevices: 2 collection(s)"));

    T_CASE("RIDEV_INPUTSINK without a target fails with ERROR_INVALID_PARAMETER and records nothing of the call");
    two[0].dwFlags = two[1].dwFlags = RIDEV_INPUTSINK;
    rt_set_last_error(ctx, 0xdeadbeef);
    T_EQ(rt_raw_input_register(two, 2, sizeof(RAWINPUTDEVICE)), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_raw_input_find(1, 5, &got, NULL), TRUE);
    T_EQ(got.dwFlags, 0);

    T_CASE("with a target window the registration replaces the record of the same collection");
    two[0].hwndTarget = two[1].hwndTarget = h;
    T_EQ(rt_raw_input_register(two, 2, sizeof(RAWINPUTDEVICE)), TRUE);
    T_EQ(rt_raw_input_count(), 2);
    T_EQ(rt_raw_input_find(1, 5, &got, &thread), TRUE);
    T_EQ(got.dwFlags, RIDEV_INPUTSINK);
    T_TRUE(got.hwndTarget == h);
    T_EQ(thread, rt_window_thread(h, NULL));

    T_CASE("the game's shape: one mouse collection for a window, 16 bytes");
    T_EQ(rt_raw_input_register(&mouse, 1, 16), TRUE);
    T_EQ(rt_raw_input_count(), 3);
    mouse.dwFlags = RIDEV_NOLEGACY;
    T_EQ(rt_raw_input_register(&mouse, 1, 16), TRUE);
    T_EQ(rt_raw_input_find(1, 2, &got, NULL), TRUE);
    T_EQ(got.dwFlags, RIDEV_NOLEGACY);

    T_CASE("RIDEV_REMOVE with no target drops the record");
    mouse.dwFlags = RIDEV_REMOVE;
    mouse.hwndTarget = NULL;
    T_EQ(rt_raw_input_register(&mouse, 1, 16), TRUE);
    T_EQ(rt_raw_input_count(), 2);
    T_EQ(rt_raw_input_find(1, 2, NULL, NULL), FALSE);
    T_EQ(rt_raw_input_find(1, 4, NULL, NULL), TRUE);

    /* the stops */
    T_TRAPS(rt_raw_input_register(&mouse, 1, 16), "rt_raw_input_register: not implemented: RIDEV_REMOVE of usage page 0x1, usage 0x2");
    mouse.hwndTarget = h;
    T_TRAPS(rt_raw_input_register(&mouse, 1, 16), "rt_raw_input_register: not implemented: RIDEV_REMOVE with a target window");
    mouse.dwFlags = RIDEV_DEVNOTIFY;
    T_TRAPS(rt_raw_input_register(&mouse, 1, 16), "rt_raw_input_register: not implemented: flags 0x2000");
    mouse.dwFlags = RIDEV_PAGEONLY;
    T_TRAPS(rt_raw_input_register(&mouse, 1, 16), "rt_raw_input_register: not implemented: flags 0x20");
    mouse.dwFlags = 0;
    mouse.hwndTarget = (HWND)(uintptr_t)0x40000FF0;
    T_TRAPS(rt_raw_input_register(&mouse, 1, 16), "rt_raw_input_register: not implemented: target");
    T_TRAPS(rt_raw_input_register(NULL, 1, 16), "rt_raw_input_register: not implemented: a NULL list");
    T_TRAPS(rt_raw_input_register(&mouse, 0, 16), "rt_raw_input_register: not implemented: a count of 0");
}
