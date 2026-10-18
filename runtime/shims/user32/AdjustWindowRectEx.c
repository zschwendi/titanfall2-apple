/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-adjustwindowrectex
 * RT-CASES:  a style with no frame: the rectangle comes back unchanged and TRUE; a caption and fixed frame grow the rectangle; does not change last error
 * RT-TRAPS:  a thick frame, a menu or a tool window's caption; a NULL rectangle
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, AdjustWindowRectEx, LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
    /* The window rectangle is the client rectangle grown by the frame Windows puts around it
     * (rt/window.h, rt_window_frame, which says where its numbers come from).  The core has
     * no number for a thick frame, a menu or a tool window's caption: those stop the run with
     * the styles.  A NULL rectangle is not documented, so it stops too.  Success sets no last
     * error. */
    RECT in;
    if (!lpRect) RT_UNIMPLEMENTED(ctx, "a NULL rectangle (not documented)");
    if (!rt_window_frame(dwStyle, dwExStyle, bMenu, &in))
        RT_UNIMPLEMENTED(ctx, "a frame this desktop has no size for: style 0x%x, extended style 0x%x, menu %d", dwStyle, dwExStyle, bMenu);
    lpRect->left -= in.left;
    lpRect->top -= in.top;
    lpRect->right += in.right;
    lpRect->bottom += in.bottom;
    return TRUE;
}
