/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/xinput/nf-xinput-xinputgetcapabilities
 * RT-CASES:  connected controller exposes supported inputs without rumble; no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167); the same for slots 1, 2 and 3; nothing is written to the structure; last error is left alone; the flag XINPUT_FLAG_GAMEPAD (1), as the game passes it, changes nothing
 * RT-TRAPS:  XInputGetCapabilities with user index; XInputGetCapabilities with a NULL; XInputGetCapabilities with flags
 */
#include "rt/shim.h"
#include "rt/gamepad.h"

/* The core copies the exact Windows structure layout without changing LastError. */
SHIM(xinput1_3, DWORD, XInputGetCapabilities, DWORD dwUserIndex, DWORD dwFlags, void *pCapabilities)
{
    if (dwUserIndex > 3) RT_UNIMPLEMENTED(ctx, "XInputGetCapabilities with user index %u, above 3", (unsigned)dwUserIndex);
    /* "Any value of dwflags other than the above [XINPUT_FLAG_GAMEPAD] or 0 is illegal and will
     * result in an error break when debugging."  XINPUT_FLAG_GAMEPAD is 1 (xinput.h; from
     * memory, and what the game passes at inputsystem+0xce4c). */
    if (dwFlags > 1) RT_UNIMPLEMENTED(ctx, "XInputGetCapabilities with flags 0x%x, neither 0 nor XINPUT_FLAG_GAMEPAD", (unsigned)dwFlags);
    if (!pCapabilities) RT_UNIMPLEMENTED(ctx, "XInputGetCapabilities with a NULL pCapabilities");
    return rt_gamepad_get_capabilities(dwUserIndex, pCapabilities);
}
