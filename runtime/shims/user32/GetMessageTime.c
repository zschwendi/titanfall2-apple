/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getmessagetime
 * RT-CASES:  0 before the thread has taken a message; a message looked at and left does not count; the time of the last message taken; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG, GetMessageTime)
{
    /* The core remembers MSG.time of the last message the thread took (rt/window.h,
     * rt_msg_time). */
    return rt_msg_time();
}
