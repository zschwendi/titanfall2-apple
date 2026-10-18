/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerwindowmessagea
 * RT-CASES:  a message number from 0xC000 to 0xFFFF; the same string gives the same number, also in other case; another string gives another number; does not change last error
 * RT-TRAPS:  core: a NULL message name; core: an empty message name; core: a name longer than 255 characters; core: a name that is an atom
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, UINT, RegisterWindowMessageA, LPCSTR lpString)
{
    /* The core keeps the table of names (rt/window.h, rt_wndmsg_register) and stops for the
     * names it does not answer; the documented failure return of 0 is never produced. */
    return rt_wndmsg_register(RT_TEXT_A(lpString));
}
