/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-findwindowa
 * RT-CASES:  finds the first top-level window whose class matches (ASCII case ignored, an atom allowed); finds by title; finds by class and title; NULL matches all; an empty title matches only a window with no title; NULL when nothing matches, with last error left alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, FindWindowA, LPCVOID lpClassName, LPCSTR lpWindowName)
{
    /* The core searches the windows of this process and sets nothing: a search that finds no
     * window returns NULL and leaves last error alone (the documentation's remark). */
    return rt_window_find(RT_TEXT_A((const char *)lpClassName), RT_TEXT_A(lpWindowName));
}
