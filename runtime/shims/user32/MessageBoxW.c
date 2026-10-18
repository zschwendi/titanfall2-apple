/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-messageboxw
 * RT-CASES:  the caption and each line of the text are logged before the run ends
 * RT-TRAPS:  core: every message box ends the run with a report that carries its caption and text
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, INT, MessageBoxW, HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    /* As MessageBoxA, with UTF-16 text (rt/window.h, rt_message_box; issue 52). */
    rt_message_box(ctx, hWnd, RT_TEXT_W(lpText), RT_TEXT_W(lpCaption), uType);
}
