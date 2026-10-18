/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-translatemessage
 * RT-CASES:  a message that is not translated returns FALSE; WM_KEYUP returns TRUE and posts nothing; WM_KEYDOWN of a character key returns TRUE and posts WM_CHAR; WM_KEYDOWN of a key without a character returns TRUE and posts nothing; the MSG is never written; does not change last error
 * RT-TRAPS:  NULL lpMsg; core: a system key message; core: a key the US table does not have; core: VK_PACKET and VK_PROCESSKEY; core: a wParam above 0xFF
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, TranslateMessage, const MSG *lpMsg)
{
    /* The work is the core's (rt_key_translate; docs/handoff/rt-contract-input.md K9 to K16):
     * the two key messages answer TRUE, a key-down of a character key posts its WM_CHAR, and
     * what is not built (the system keys, a key outside the US table, Shift+Enter and
     * Control+Esc, which need a key state only an input script gives: tests/core/input_keys.c)
     * stops there with the key.  The MSG is only read. */
    if (!lpMsg) RT_UNIMPLEMENTED(ctx, "NULL lpMsg: no message to translate");
    return rt_key_translate(ctx, lpMsg);
}
