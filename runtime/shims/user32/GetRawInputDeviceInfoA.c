/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getrawinputdeviceinfoa
 * RT-CASES:  NULL size pointer gives ERROR_NOACCESS; every handle is invalid gives ERROR_INVALID_HANDLE; nothing is read or written; no device attached is logged once
 * RT-TRAPS:  the mouse or the keyboard of an input script
 */
#include "rt/shim.h"
#include "rt/rawinput.h"            /* the devices of the scripted input source: none without a script */

SHIM(user32, UINT, GetRawInputDeviceInfoA, HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize)
{
    /* No input device is attached to this machine before Gate 4 (docs/RUNTIME.md 9.2). */
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "user32.dll!GetRawInputDeviceInfoA",
                "GetRawInputDeviceInfoA: no raw-input device is attached");
    /* The size pointer is checked before the handle. */
    if (!pcbSize) return FAIL(ctx, ERROR_NOACCESS, (UINT)-1);
    /* With an input script two handles exist (rt/rawinput.h); what describes them (name, the
     * RID_DEVICE_INFO of a generic mouse and keyboard) is not built, and the game does not ask. */
    if (rt_raw_input_devices(NULL, 0) && (hDevice == RT_HRAWDEV_MOUSE || hDevice == RT_HRAWDEV_KEYBOARD))
        RT_UNIMPLEMENTED(ctx, "the mouse or the keyboard of an input script: command 0x%x for device %p", uiCommand, hDevice);
    /* No other device handle was ever handed out, so every other handle is invalid; nothing is read or written. */
    (void)hDevice;
    (void)uiCommand;
    (void)pData;
    return FAIL(ctx, ERROR_INVALID_HANDLE, (UINT)-1);
}
