/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/xinput/nf-xinput-xinputsetstate
 * RT-CASES:  no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167); the same for slots 1, 2 and 3; nothing is written to the structure; last error is left alone
 * RT-TRAPS:  XInputSetState with user index; XInputSetState with a NULL
 */
#include "rt/shim.h"

/* The structure is passed as an untyped pointer: this function never reads or writes it. */
SHIM(xinput1_3, DWORD, XInputSetState, DWORD dwUserIndex, void *pVibration)
{
    /* RT-DOC, read 2026-10-10: "Index of the user's controller. Can be a value from 0 to 3."; "If
     * the controller is not connected, the return value is ERROR_DEVICE_NOT_CONNECTED."; "The
     * function does not use SetLastError to set the calling thread's last-error code."  The
     * number 1167 (0x48F) is Microsoft's list of system error codes 1000 to 1299, read the
     * same day.  This runtime has no controller in any of the four slots (the planner's
     * decision, brief of unit d3d-resources), so that is the answer for every index the page
     * allows.  The page says nothing of what the structure holds in that case: nothing is
     * written.  It names no result for an index above 3 or for a NULL pointer: both stop. */
    if (dwUserIndex > 3) RT_UNIMPLEMENTED(ctx, "XInputSetState with user index %u, above 3", (unsigned)dwUserIndex);
    if (!pVibration) RT_UNIMPLEMENTED(ctx, "XInputSetState with a NULL pVibration");
    return 1167;        /* ERROR_DEVICE_NOT_CONNECTED: returned, not set as the last error */
}
