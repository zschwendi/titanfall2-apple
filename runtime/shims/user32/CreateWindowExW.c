/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-createwindowexw
 * RT-CASES:  a hidden popup of a W class: its handle and the five creation messages; a popup's rectangle is its position and size; a negative size is taken as zero; GWL_STYLE of a popup: 0x84000000, with WS_CLIPSIBLINGS; a child: the five messages, then its parent gets WM_PARENTNOTIFY; two children of one parent are in creation order; no such class: NULL and ERROR_CANNOT_FIND_WND_CLASS; a child with no parent: NULL and ERROR_TLW_WITH_WSCHILD, the procedure is not called; a parent that is not a window: NULL and ERROR_INVALID_WINDOW_HANDLE; does not change last error
 * RT-TRAPS:  core: a predefined control class; core: a class atom below 0xC000; core: an overlapped or thick-framed window; core: a procedure that refuses WM_NCCREATE; core: a procedure that refuses WM_CREATE; core: a WS_CHILD window of the desktop; core: a message-only window; core: a class of the other character type; core: WS_VISIBLE at creation; core: WS_MINIMIZE or WS_MAXIMIZE; core: CW_USEDEFAULT as a position or a size; core: WS_CHILD together with WS_POPUP; core: a menu for a window that is not a child
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, CreateWindowExW, DWORD dwExStyle, LPCVOID lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, INT X, INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    /* The core does all of it, sets last error when it fails and stops for what it does not answer (rt/window.h, rt_window_create).  The messages go to the class procedure on this thread. */
    const RtWindowCreate a = { .exstyle = dwExStyle, .style = dwStyle, .cls = RT_TEXT_W(lpClassName), .title = RT_TEXT_W(lpWindowName),
                               .x = X, .y = Y, .cx = nWidth, .cy = nHeight, .parent = hWndParent, .menu = hMenu,
                               .instance = hInstance, .param = lpParam, .unicode = true };
    return rt_window_create(ctx, &a);
}
