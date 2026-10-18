#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

TEST(user32, GetWindowThreadProcessId)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(T_CTX(), &a);
    HWND gone = rt_window_create(T_CTX(), &a);
    HWND f = (HWND)0x40000FF0;
    DWORD tid, pid, want_tid, want_pid;

    rt_window_destroy(T_CTX(), gone);
    want_tid = CALL(kernel32, GetCurrentThreadId);
    want_pid = CALL(kernel32, GetCurrentProcessId);

    T_CASE("returns the id of the thread that created the window");
    pid = 0;
    tid = CALL(user32, GetWindowThreadProcessId, h, &pid);
    T_EQ(tid, want_tid);

    T_CASE("stores the id of the window's process when a pointer is given");
    T_EQ(pid, want_pid);

    T_CASE("a NULL pointer is allowed");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetWindowThreadProcessId, h, NULL), want_tid);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an invalid window returns 0 with ERROR_INVALID_WINDOW_HANDLE and leaves the DWORD alone");
    pid = 0x5555AAAA;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindowThreadProcessId, NULL, &pid), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(pid, 0x5555AAAA);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindowThreadProcessId, f, &pid), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(pid, 0x5555AAAA);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindowThreadProcessId, gone, &pid), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(pid, 0x5555AAAA);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetWindowThreadProcessId, h, &pid);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, GetWindowThreadProcessId, RT_HWND_DESKTOP, &pid), "rt_window_thread: not implemented");
}
