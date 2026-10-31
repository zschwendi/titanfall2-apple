/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/xinput/nf-xinput-xinputsetstate
 * RT-CASES:  connected controller reports unsupported rumble; no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167); the same for slots 1, 2 and 3; nothing is written to the structure; last error is left alone
 * RT-TRAPS:  XInputSetState with user index; XInputSetState with a NULL
 */
#include "rt/shim.h"
#include "rt/gamepad.h"

/* The core copies the exact Windows structure layout without changing LastError. */
SHIM(xinput1_3, DWORD, XInputSetState, DWORD dwUserIndex, void *pVibration)
{
    if (dwUserIndex > 3) RT_UNIMPLEMENTED(ctx, "XInputSetState with user index %u, above 3", (unsigned)dwUserIndex);
    if (!pVibration) RT_UNIMPLEMENTED(ctx, "XInputSetState with a NULL pVibration");
    return rt_gamepad_set_vibration(dwUserIndex, pVibration);
}
