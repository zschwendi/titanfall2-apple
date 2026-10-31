/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/xinput/nf-xinput-xinputgetstate
 * RT-CASES:  connected controller uses native snapshot; no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167); the same for slots 1, 2 and 3; nothing is written to the structure; last error is left alone
 * RT-TRAPS:  XInputGetState with user index; XInputGetState with a NULL
 */
#include "rt/shim.h"
#include "rt/gamepad.h"

/* The core copies the exact Windows structure layout without changing LastError. */
SHIM(xinput1_3, DWORD, XInputGetState, DWORD dwUserIndex, void *pState)
{
    if (dwUserIndex > 3) RT_UNIMPLEMENTED(ctx, "XInputGetState with user index %u, above 3", (unsigned)dwUserIndex);
    if (!pState) RT_UNIMPLEMENTED(ctx, "XInputGetState with a NULL pState");
    return rt_gamepad_get_state(dwUserIndex, pState);
}
