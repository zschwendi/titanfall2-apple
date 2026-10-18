/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-unregisterclassa
 * RT-CASES:  unregisters a class by its name; the name is compared without regard to case; unregisters a class by its atom; a second unregistration fails with ERROR_CLASS_DOES_NOT_EXIST; a NULL name fails with ERROR_CLASS_DOES_NOT_EXIST; an empty name fails with ERROR_CLASS_DOES_NOT_EXIST; a name longer than 255 characters fails with ERROR_CLASS_DOES_NOT_EXIST; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, UnregisterClassA, LPCVOID lpClassName, HINSTANCE hInstance)
{
    /* The name is a code page 1252 string, or a class atom in the low word (the core tells them
     * apart).  The core checks the class, unregisters it and sets last error when it fails. */
    return rt_wndclass_unregister(RT_TEXT_A((const char *)lpClassName), hInstance);
}
