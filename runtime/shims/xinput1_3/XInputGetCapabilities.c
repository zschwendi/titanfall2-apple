/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/xinput/nf-xinput-xinputgetcapabilities
 * RT-CASES:  no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167); the same for slots 1, 2 and 3; nothing is written to the structure; last error is left alone; the flag XINPUT_FLAG_GAMEPAD (1), as the game passes it, changes nothing
 * RT-TRAPS:  XInputGetCapabilities with user index; XInputGetCapabilities with a NULL; XInputGetCapabilities with flags
 */
#include "rt/shim.h"

/* The structure is passed as an untyped pointer: this function never reads or writes it. */
SHIM(xinput1_3, DWORD, XInputGetCapabilities, DWORD dwUserIndex, DWORD dwFlags, void *pCapabilities)
{
    /* RT-DOC, read 2026-10-10: "Index of the user's controller. Can be a value in the range"
     * 0 to 3; "If the controller is not connected, the return value is ERROR_DEVICE_NOT_CONNECTED."; "The
     * function does not use SetLastError to set the calling thread's last-error code."  The
     * number 1167 (0x48F) is Microsoft's list of system error codes 1000 to 1299, read the
     * same day.  This runtime has no controller in any of the four slots (the planner's
     * decision, brief of unit d3d-resources), so that is the answer for every index the page
     * allows.  The page says nothing of what the structure holds in that case: nothing is
     * written.  It names no result for an index above 3 or for a NULL pointer: both stop. */
    if (dwUserIndex > 3) RT_UNIMPLEMENTED(ctx, "XInputGetCapabilities with user index %u, above 3", (unsigned)dwUserIndex);
    /* "Any value of dwflags other than the above [XINPUT_FLAG_GAMEPAD] or 0 is illegal and will
     * result in an error break when debugging."  XINPUT_FLAG_GAMEPAD is 1 (xinput.h; from
     * memory, and what the game passes at inputsystem+0xce4c). */
    if (dwFlags > 1) RT_UNIMPLEMENTED(ctx, "XInputGetCapabilities with flags 0x%x, neither 0 nor XINPUT_FLAG_GAMEPAD", (unsigned)dwFlags);
    if (!pCapabilities) RT_UNIMPLEMENTED(ctx, "XInputGetCapabilities with a NULL pCapabilities");
    return 1167;        /* ERROR_DEVICE_NOT_CONNECTED: returned, not set as the last error */
}
