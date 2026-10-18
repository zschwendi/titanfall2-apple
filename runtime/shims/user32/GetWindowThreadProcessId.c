/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid
 * RT-CASES:  returns the id of the thread that created the window; stores the id of the window's process when a pointer is given; a NULL pointer is allowed; an invalid window returns 0 with ERROR_INVALID_WINDOW_HANDLE and leaves the DWORD alone; success does not change last error
 * RT-TRAPS:  core: RT_HWND_DESKTOP
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, DWORD, GetWindowThreadProcessId, HWND hWnd, LPDWORD lpdwProcessId)
{
    /* The core does all of it: it answers the owner thread's id, writes *pid only for a
     * window (not for an invalid handle), sets ERROR_INVALID_WINDOW_HANDLE for an invalid
     * one, and stops for RT_HWND_DESKTOP (rt/window.h). */
    return rt_window_thread(hWnd, lpdwProcessId);
}
