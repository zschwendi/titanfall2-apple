/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-translateacceleratora
 * RT-CASES:  NULL table returns 0 with ERROR_INVALID_ACCEL_HANDLE; (HACCEL)0x1234 returns 0 with ERROR_INVALID_ACCEL_HANDLE; NULL and non-NULL MSG with preset bytes unchanged; logs its line once at debug level
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/user.h"

SHIM(user32, INT, TranslateAcceleratorA, HWND hWnd, HACCEL hAccTable, LPMSG lpMsg)
{
    /* No accelerator table exists on this machine: no boot-path DLL imports a function that
     * creates or loads one, so every HACCEL is NULL or invalid. The documented failure, 0, with
     * ERROR_INVALID_ACCEL_HANDLE. Nothing is read or written: the window and the message are
     * not looked at. */
    (void)hWnd;
    (void)hAccTable;
    (void)lpMsg;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "user32.dll!TranslateAcceleratorA", "TranslateAcceleratorA: no accelerator table exists");
    return FAIL(ctx, ERROR_INVALID_ACCEL_HANDLE, 0);
}
