/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-callwindowprocw
 * RT-CASES:  the four arguments arrive in order and the 64-bit result comes back
 * RT-TRAPS:  core: a NULL procedure
 */
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/gdi.h"

SHIM(user32, LRESULT, CallWindowProcW, WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    /* A procedure is a plain guest address here: it is called and nothing is converted (rt/window.h, rt_window_call_proc). */
    return rt_window_call_proc(ctx, lpPrevWndFunc, hWnd, Msg, wParam, lParam, true);
}
