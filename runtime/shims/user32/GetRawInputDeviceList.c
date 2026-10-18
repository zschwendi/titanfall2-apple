/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getrawinputdevicelist
 * RT-CASES:  size query with NULL list gives 0 devices and count 0; list given gives 0 devices, count unchanged, list not written; cbSize not 16 gives ERROR_INVALID_PARAMETER; NULL count with cbSize 16 gives ERROR_NOACCESS; success does not change last error; no device attached is logged once; with an input script the size query gives 2 and a list of two gets the mouse and the keyboard
 * RT-TRAPS:  with an input script a list with room for fewer than 2 devices
 */
#include "rt/shim.h"
#include "rt/win/user.h"            /* PRAWINPUTDEVICELIST, RAWINPUTDEVICELIST */
#include "rt/rawinput.h"            /* the devices of the scripted input source: none without a script */

SHIM(user32, UINT, GetRawInputDeviceList, PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize)
{
    /* No input device is attached to this machine before Gate 4 (docs/RUNTIME.md 9.2). */
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "user32.dll!GetRawInputDeviceList",
                "GetRawInputDeviceList: no raw-input device is attached");
    /* 16 is sizeof(RAWINPUTDEVICELIST). The size is checked before the count pointer. */
    if (cbSize != 16) return FAIL(ctx, ERROR_INVALID_PARAMETER, (UINT)-1);
    if (!puiNumDevices) return FAIL(ctx, ERROR_NOACCESS, (UINT)-1);
    /* Size query (NULL list): the count is the number of devices, 0. With a list, the count must be
     * at least that number (always, with none), and the count and the list are left unchanged. */
    RtRawDevice dev[2];
    const unsigned have = rt_raw_input_devices(dev, 2);
    if (!have) {
        if (!pRawInputDeviceList) *puiNumDevices = 0;
        return 0;
    }
    /* An input script is loaded: a mouse and a keyboard (rt/rawinput.h).  The size query stores the
     * number and returns 0; a list with room gets them and the number is returned (the page). */
    if (!pRawInputDeviceList) { *puiNumDevices = have; return 0; }
    if (*puiNumDevices < have)
        RT_UNIMPLEMENTED(ctx, "with an input script a list with room for fewer than 2 devices: %u (Windows: -1 with "
                         "ERROR_INSUFFICIENT_BUFFER and the number needed, from memory)", *puiNumDevices);
    for (unsigned i = 0; i < have; i++) pRawInputDeviceList[i] = (RAWINPUTDEVICELIST){ .hDevice = dev[i].device, .dwType = dev[i].type };
    return have;
}
