/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-messageboxa
 * RT-CASES:  the caption and each line of the text are logged before the run ends
 * RT-TRAPS:  core: every message box ends the run with a report that carries its caption and text
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, INT, MessageBoxA, HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    /* Nobody can read a box or press its button: the caption and the text go to the log and
     * to the fatal report, and the run ends (rt/window.h, rt_message_box; issue 52). */
    rt_message_box(ctx, hWnd, RT_TEXT_A(lpText), RT_TEXT_A(lpCaption), uType);
}
