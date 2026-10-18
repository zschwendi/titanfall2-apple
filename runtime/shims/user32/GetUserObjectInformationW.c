/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getuserobjectinformationw
 * RT-CASES:  UOI_FLAGS of the window station (last error left alone); a length below 12 or a NULL buffer fails with ERROR_INSUFFICIENT_BUFFER and stores 12; a NULL length pointer is allowed
 * RT-TRAPS:  an index other than UOI_FLAGS; a handle other than the window station
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, GetUserObjectInformationW, HANDLE hObj, INT nIndex, LPVOID pvInfo, DWORD nLength, LPDWORD lpnLengthNeeded)
{
    /* The core answers one question: the flags of the window station (UOI_FLAGS), which the C
     * runtime asks before a message box. Any other handle or index stops the run with the value. */
    DWORD need = (DWORD)sizeof(USEROBJECTFLAGS);
    USEROBJECTFLAGS *fl;

    if (hObj != RT_HWINSTA) RT_UNIMPLEMENTED(ctx, "a handle other than the window station: %p", (void *)hObj);
    if (nIndex != UOI_FLAGS) RT_UNIMPLEMENTED(ctx, "an index other than UOI_FLAGS: %d", nIndex);
    if (!pvInfo || nLength < need) {
        if (lpnLengthNeeded) *lpnLengthNeeded = need;
        return FAIL(ctx, ERROR_INSUFFICIENT_BUFFER, FALSE);
    }
    fl = (USEROBJECTFLAGS *)pvInfo;
    fl->fInherit = FALSE;
    fl->fReserved = FALSE;
    fl->dwFlags = WSF_VISIBLE;
    if (lpnLengthNeeded) *lpnLengthNeeded = need;
    return TRUE;
}
