/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getkeystate
 * RT-CASES:  returns 0 for keys 0, 0x10, 0x14, 0x41 and 255; does not change last error
 * RT-TRAPS:  core: vk outside 0-255
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, SHORT, GetKeyState, INT nVirtKey)
{
    /* A key outside 0-255 is a stop of the core (rt_key_state): no guard here. */
    return rt_key_state(nVirtKey);
}
