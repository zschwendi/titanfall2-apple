/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-defwindowprocw
 * RT-CASES:  WM_NCCREATE stores the title and returns 1; WM_NCCALCSIZE shrinks the client rectangle by the frame and returns 0; WM_NCCALCSIZE with a NULL lParam returns 0; WM_CREATE WM_DESTROY WM_NCDESTROY WM_NULL WM_MOVE WM_SIZE WM_PARENTNOTIFY return 0; WM_CLOSE destroys the window and returns 0; WM_SETTEXT WM_GETTEXT WM_GETTEXTLENGTH store and copy the text; WM_WINDOWPOSCHANGING returns 0; WM_WINDOWPOSCHANGED sends WM_MOVE and WM_SIZE as its flags say; WM_ACTIVATE moves the focus and returns 0; WM_NCACTIVATE returns TRUE; WM_SHOWWINDOW WM_ACTIVATEAPP WM_SETFOCUS WM_KILLFOCUS return 0; user, application and registered messages return 0; an invalid window returns 0
 * RT-TRAPS:  core: message 0x0024 WM_GETMINMAXINFO; core: message 0x0200 WM_MOUSEMOVE; core: message 0x10000 above 0xFFFF; core: WM_SETTEXT on a window of the other character type; core: WM_GETTEXT on a window of the other character type
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LRESULT, DefWindowProcW, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return rt_window_def_proc(ctx, hWnd, Msg, wParam, lParam, true);
}
