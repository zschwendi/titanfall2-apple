/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerclassa
 * RT-CASES:  a class gives an atom of 0xC000 or above and is stored under its name with its instance; the same name twice fails with ERROR_CLASS_ALREADY_EXISTS; a NULL structure, a NULL name and a negative extra size fail with ERROR_INVALID_PARAMETER; a name longer than 255 characters fails with ERROR_INVALID_PARAMETER; success does not change last error
 * RT-TRAPS:  core: a class name that is an atom; core: a class with no window procedure; core: a class with a menu name; core: more than 40 extra bytes; core: an empty class name; core: a name registered with another hInstance; core: more than 256 classes
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, ATOM, RegisterClassA, const WNDCLASSA *lpWndClass)
{
    /* WNDCLASSA is WNDCLASSEXA without cbSize and hIconSm: the core takes the larger one
     * (rt/window.h, rt_wndclass_register), which checks, stores and sets last error. */
    if (!lpWndClass) return rt_wndclass_register(NULL, false);
    WNDCLASSEXA ex = {
        .cbSize = sizeof ex,
        .style = lpWndClass->style,
        .lpfnWndProc = lpWndClass->lpfnWndProc,
        .cbClsExtra = lpWndClass->cbClsExtra,
        .cbWndExtra = lpWndClass->cbWndExtra,
        .hInstance = lpWndClass->hInstance,
        .hIcon = lpWndClass->hIcon,
        .hCursor = lpWndClass->hCursor,
        .hbrBackground = lpWndClass->hbrBackground,
        .lpszMenuName = lpWndClass->lpszMenuName,
        .lpszClassName = lpWndClass->lpszClassName,
        .hIconSm = NULL,
    };
    return rt_wndclass_register((const WNDCLASSEXW *)&ex, false);
}
